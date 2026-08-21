#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "WebToUESession.h"

class SWidget;
class IWebToUEFeedbackBackend;
class IWebToUEFeedbackResourceProvider;
class IWebToUEFeedbackSettingsProvider;
class ULocalPlayer;
class UWebToUEDocument;
class UWebToUEFeedbackProfile;
class UWebToUEView;
class UWorld;

/** Injectable Screen layer used by the production LocalPlayer adapter and deterministic tests. */
class WEBTOUERUNTIME_API IWebToUEScreenLayer
{
public:
	virtual ~IWebToUEScreenLayer() = default;
	virtual ULocalPlayer* GetLocalPlayer() const = 0;
	virtual UWorld* GetWorld() const = 0;
	virtual bool AddWidget(TSharedRef<SWidget> Widget, int32 ZOrder, FString& OutError) = 0;
	virtual void RemoveWidget(TSharedRef<SWidget> Widget) = 0;
};

struct WEBTOUERUNTIME_API FWebToUEScreenHostCreateParams
{
	UWebToUEDocument* Document = nullptr;
	UObject* DataContext = nullptr;
	UObject* CommandContext = nullptr;
	FName SurfaceId = TEXT("webtoue.screen.default");
	FWebToUEEnvironmentContext Environment;
	TSharedPtr<IWebToUEClock> Clock;
	/** Explicit Router wins; otherwise a Profile creates the default UE Router. */
	TSharedPtr<IWebToUEFeedbackRouter> FeedbackRouter;
	UWebToUEFeedbackProfile* FeedbackProfile = nullptr;
	TSharedPtr<IWebToUEFeedbackBackend> FeedbackBackend;
	TSharedPtr<IWebToUEFeedbackSettingsProvider> FeedbackSettingsProvider;
	TSharedPtr<IWebToUEFeedbackResourceProvider> FeedbackResourceProvider;
	int32 ZOrder = 0;
	bool bRespectSafeZone = true;
};

using FWebToUEScreenContentWrapper =
	TFunction<TSharedRef<SWidget>(TSharedRef<SWidget> /* Content */)>;

/**
 * Code-driven LocalPlayer Screen Host for one WebToUE View and UI Session.
 *
 * Creation, attachment and shutdown are Game Thread-only. Shutdown invalidates the Session before
 * removing the Slate content so delayed feedback and future callbacks cannot target a stale View.
 */
class WEBTOUERUNTIME_API FWebToUEScreenHost final
{
public:
	~FWebToUEScreenHost();

	static TUniquePtr<FWebToUEScreenHost> CreateForLocalPlayer(
		ULocalPlayer* LocalPlayer,
		const FWebToUEScreenHostCreateParams& Params,
		FString& OutError);
	static TUniquePtr<FWebToUEScreenHost> CreateWithLayer(
		TSharedRef<IWebToUEScreenLayer> Layer,
		const FWebToUEScreenHostCreateParams& Params,
		FString& OutError);

	bool BuildContent(FString& OutError);
	bool Attach(const FWebToUEScreenContentWrapper& Wrapper, FString& OutError);
	bool Attach(FString& OutError) { return Attach(FWebToUEScreenContentWrapper(), OutError); }
	void Shutdown();

	bool IsAttached() const { return bAttached; }
	UWebToUEView* GetView() const { return View.Get(); }
	TSharedPtr<FWebToUESession> GetSession() const { return Session; }
	TSharedPtr<SWidget> GetContentWidget() const { return ContentWidget; }
	TSharedPtr<SWidget> GetHostedWidget() const { return HostedWidget; }

private:
	FWebToUEScreenHost() = default;

	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void HandleLocalPlayerRemoved(ULocalPlayer* LocalPlayer);
	void BindLifecycle();
	void UnbindLifecycle();

	TSharedPtr<IWebToUEScreenLayer> Layer;
	TSharedPtr<FWebToUESession> Session;
	TStrongObjectPtr<UWebToUEView> View;
	TSharedPtr<SWidget> ContentWidget;
	TSharedPtr<SWidget> HostedWidget;
	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle LocalPlayerRemovedHandle;
	int32 ZOrder = 0;
	bool bAttached = false;
};
