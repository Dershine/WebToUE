#include "WebToUEScreenHost.h"

#include "WebToUEDocument.h"
#include "WebToUEView.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Widgets/SWidget.h"

namespace WebToUE::ScreenHost::Private
{
	class FLocalPlayerScreenLayer final : public IWebToUEScreenLayer
	{
	public:
		explicit FLocalPlayerScreenLayer(ULocalPlayer* InLocalPlayer)
			: LocalPlayer(InLocalPlayer)
		{
		}

		virtual ULocalPlayer* GetLocalPlayer() const override
		{
			return LocalPlayer.Get();
		}

		virtual UWorld* GetWorld() const override
		{
			const ULocalPlayer* Player = LocalPlayer.Get();
			return Player ? Player->GetWorld() : nullptr;
		}

		virtual bool AddWidget(
			TSharedRef<SWidget> Widget, int32 ZOrder, FString& OutError) override
		{
			OutError.Reset();
			ULocalPlayer* Player = LocalPlayer.Get();
			UGameViewportClient* Viewport = Player
				? Cast<UGameViewportClient>(Player->GetViewportClient()) : nullptr;
			if (!Player || !Viewport)
			{
				OutError = TEXT("Screen Host requires a LocalPlayer GameViewportClient.");
				return false;
			}
			ViewportClient = Viewport;
			Viewport->AddViewportWidgetForPlayer(Player, Widget, ZOrder);
			return true;
		}

		virtual void RemoveWidget(TSharedRef<SWidget> Widget) override
		{
			if (UGameViewportClient* Viewport = ViewportClient.Get())
			{
				Viewport->RemoveViewportWidgetForPlayer(LocalPlayer.Get(), Widget);
			}
			ViewportClient.Reset();
		}

	private:
		TWeakObjectPtr<ULocalPlayer> LocalPlayer;
		TWeakObjectPtr<UGameViewportClient> ViewportClient;
	};
}

FWebToUEScreenHost::~FWebToUEScreenHost()
{
	if (ensureMsgf(IsInGameThread(), TEXT("WebToUE Screen Host must be destroyed on the Game Thread.")))
	{
		Shutdown();
	}
}

TUniquePtr<FWebToUEScreenHost> FWebToUEScreenHost::CreateForLocalPlayer(
	ULocalPlayer* LocalPlayer,
	const FWebToUEScreenHostCreateParams& Params,
	FString& OutError)
{
	return CreateWithLayer(
		MakeShared<WebToUE::ScreenHost::Private::FLocalPlayerScreenLayer>(LocalPlayer),
		Params, OutError);
}

TUniquePtr<FWebToUEScreenHost> FWebToUEScreenHost::CreateWithLayer(
	TSharedRef<IWebToUEScreenLayer> InLayer,
	const FWebToUEScreenHostCreateParams& Params,
	FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("WebToUE Screen Host creation is Game Thread-only.");
		return nullptr;
	}
	if (!Params.Document)
	{
		OutError = TEXT("WebToUE Screen Host requires a compiled Document.");
		return nullptr;
	}
	ULocalPlayer* LocalPlayer = InLayer->GetLocalPlayer();
	UWorld* World = InLayer->GetWorld();
	if (!LocalPlayer || !World)
	{
		OutError = TEXT("WebToUE Screen Host requires valid LocalPlayer and World context.");
		return nullptr;
	}

	FWebToUESessionCreateParams SessionParams;
	SessionParams.LocalPlayer = LocalPlayer;
	SessionParams.World = World;
	SessionParams.Surface.Kind = EWebToUESurfaceKind::Screen;
	SessionParams.Surface.SurfaceId = Params.SurfaceId;
	SessionParams.Surface.Owner = LocalPlayer;
	SessionParams.DataContext = Params.DataContext;
	SessionParams.CommandContext = Params.CommandContext;
	SessionParams.Environment = Params.Environment;
	SessionParams.Clock = Params.Clock;
	SessionParams.FeedbackRouter = Params.FeedbackRouter;
	TSharedPtr<FWebToUESession> NewSession = FWebToUESession::Create(SessionParams, OutError);
	if (!NewSession)
	{
		return nullptr;
	}

	TUniquePtr<FWebToUEScreenHost> Result(new FWebToUEScreenHost());
	Result->Layer = InLayer;
	Result->Session = NewSession;
	Result->ZOrder = Params.ZOrder;
	UWebToUEView* NewView = NewObject<UWebToUEView>(LocalPlayer);
	NewView->SetDocument(Params.Document);
	NewView->SetDataContext(Params.DataContext);
	NewView->SetRespectSafeZone(Params.bRespectSafeZone);
	NewView->SetSession(NewSession);
	Result->View.Reset(NewView);
	Result->ContentWidget = NewView->TakeWidget();
	if (!Result->ContentWidget)
	{
		NewSession->Invalidate();
		OutError = TEXT("WebToUE Screen Host failed to build its View content.");
		return nullptr;
	}
	return Result;
}

bool FWebToUEScreenHost::Attach(
	const FWebToUEScreenContentWrapper& Wrapper, FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("WebToUE Screen Host attachment is Game Thread-only.");
		return false;
	}
	if (bAttached)
	{
		OutError = TEXT("WebToUE Screen Host is already attached.");
		return false;
	}
	if (!Session || !Session->IsActive() || !ContentWidget || !Layer)
	{
		OutError = TEXT("WebToUE Screen Host cannot attach an inactive Session.");
		return false;
	}

	HostedWidget = Wrapper ? Wrapper(ContentWidget.ToSharedRef()) : ContentWidget;
	if (!HostedWidget || !Layer->AddWidget(HostedWidget.ToSharedRef(), ZOrder, OutError))
	{
		HostedWidget.Reset();
		return false;
	}
	bAttached = true;
	BindLifecycle();
	return true;
}

void FWebToUEScreenHost::Shutdown()
{
	check(IsInGameThread());
	UnbindLifecycle();
	if (Session)
	{
		Session->Invalidate();
	}
	if (View)
	{
		View->ClearSession();
	}
	if (bAttached && Layer && HostedWidget)
	{
		Layer->RemoveWidget(HostedWidget.ToSharedRef());
	}
	bAttached = false;
	HostedWidget.Reset();
	ContentWidget.Reset();
	if (View)
	{
		View->ReleaseSlateResources(true);
	}
	View.Reset();
	Layer.Reset();
}

void FWebToUEScreenHost::BindLifecycle()
{
	if (!WorldCleanupHandle.IsValid())
	{
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(
			this, &FWebToUEScreenHost::HandleWorldCleanup);
	}
	if (!LocalPlayerRemovedHandle.IsValid() && Layer)
	{
		if (ULocalPlayer* LocalPlayer = Layer->GetLocalPlayer())
		{
			if (UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
			{
				LocalPlayerRemovedHandle = GameInstance->OnLocalPlayerRemovedEvent.AddRaw(
					this, &FWebToUEScreenHost::HandleLocalPlayerRemoved);
			}
		}
	}
}

void FWebToUEScreenHost::UnbindLifecycle()
{
	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}
	if (LocalPlayerRemovedHandle.IsValid() && Layer)
	{
		if (ULocalPlayer* LocalPlayer = Layer->GetLocalPlayer())
		{
			if (UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
			{
				GameInstance->OnLocalPlayerRemovedEvent.Remove(LocalPlayerRemovedHandle);
			}
		}
		LocalPlayerRemovedHandle.Reset();
	}
}

void FWebToUEScreenHost::HandleWorldCleanup(
	UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
{
	if (Layer && Layer->GetWorld() == InWorld)
	{
		Shutdown();
	}
}

void FWebToUEScreenHost::HandleLocalPlayerRemoved(ULocalPlayer* InLocalPlayer)
{
	if (Layer && Layer->GetLocalPlayer() == InLocalPlayer)
	{
		Shutdown();
	}
}
