#include "WebToUEView.h"

#include "SWebToUEView.h"
#include "WebToUEDocument.h"
#include "WebToUESession.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSafeZone.h"

UWebToUEView::UWebToUEView(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UWebToUEView::RebuildWidget()
{
	SlateView = SNew(SWebToUEView).Owner(this);
	FName SurfaceId(TEXT("view.standalone"));
	if (const TSharedPtr<FWebToUESession> ActiveSession = Session.Pin())
	{
		SurfaceId = ActiveSession->GetSurface().SurfaceId;
	}
	SlateView->HandleSurfaceChanged(SurfaceId);
	SafeZone = SNew(SSafeZone)
		.IsTitleSafe(false)
		.SafeAreaScale(bRespectSafeZone ? FMargin(1.0f) : FMargin(0.0f))
		[
			SlateView.ToSharedRef()
		];
	if (!DocumentChangedHandle.IsValid())
	{
		DocumentChangedHandle = UWebToUEDocument::OnDocumentChanged().AddUObject(this, &UWebToUEView::HandleDocumentChanged);
	}
	return SafeZone.ToSharedRef();
}

void UWebToUEView::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	ApplySafeZoneSetting();
	if (SlateView)
	{
		SlateView->SetDocument(Document);
		SlateView->RefreshBindings(DataContext);
		BindFieldNotifications();
	}
}

void UWebToUEView::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	UnbindFieldNotifications();
	SafeZone.Reset();
	SlateView.Reset();
	if (DocumentChangedHandle.IsValid())
	{
		UWebToUEDocument::OnDocumentChanged().Remove(DocumentChangedHandle);
		DocumentChangedHandle.Reset();
	}
}

void UWebToUEView::SetDocument(UWebToUEDocument* InDocument)
{
	if (Document == InDocument) return;
	if (TSharedPtr<FWebToUESession> ActiveSession = Session.Pin())
	{
		ActiveSession->AdvanceGeneration();
	}
	Document = InDocument;
	if (SlateView)
	{
		SlateView->SetDocument(Document);
		SlateView->RefreshBindings(DataContext);
		BindFieldNotifications();
	}
}

void UWebToUEView::SetSession(TSharedPtr<FWebToUESession> InSession)
{
	Session = InSession;
	if (SlateView)
	{
		SlateView->HandleSurfaceChanged(InSession
			? InSession->GetSurface().SurfaceId : FName(TEXT("view.standalone")));
	}
}

void UWebToUEView::ClearSession()
{
	Session.Reset();
	if (SlateView) SlateView->HandleSurfaceChanged(NAME_None);
}

void UWebToUEView::SetDataContext(UObject* InDataContext)
{
	if (DataContext == InDataContext) return;
	UnbindFieldNotifications();
	DataContext = InDataContext;
	RefreshBindings();
	BindFieldNotifications();
}

bool UWebToUEView::RequestLazyResource(const FString& ResourceId)
{
	return SlateView && SlateView->RequestLazyResource(ResourceId);
}

FWebToUEMaterialParameterSubmitOutcome UWebToUEView::SubmitMaterialParameter(
	const FWebToUEMaterialParameterSubmission& Submission)
{
	if (!SlateView)
	{
		return { EWebToUEMaterialParameterSubmitResult::RejectedInactive,
			TEXT("WTUE-MAT-004: the Runtime View is inactive.") };
	}
	return SlateView->SubmitMaterialParameter(Submission);
}

FWebToUEInstanceHandle UWebToUEView::FindElementById(const FString& Id) const
{
	return SlateView ? SlateView->FindElementById(Id) : FWebToUEInstanceHandle{};
}

void UWebToUEView::RefreshBindings()
{
	if (SlateView) SlateView->RefreshBindings(DataContext);
}

void UWebToUEView::SetRespectSafeZone(bool bInRespectSafeZone)
{
	if (bRespectSafeZone == bInRespectSafeZone) return;
	bRespectSafeZone = bInRespectSafeZone;
	ApplySafeZoneSetting();
}

void UWebToUEView::ApplySafeZoneSetting()
{
	if (!SafeZone) return;
	SafeZone->SetSafeAreaScale(
		bRespectSafeZone ? FMargin(1.0f) : FMargin(0.0f));
	SafeZone->Invalidate(EInvalidateWidgetReason::Layout);
}

void UWebToUEView::HandleRuntimeEvent(FName EventName, FName ElementId)
{
	OnUIEvent.Broadcast(EventName, ElementId);
}

void UWebToUEView::GetSemanticNodes(TArray<FWebToUESemanticNode>& OutNodes) const
{
	if (SlateView) SlateView->GetSemanticNodes(OutNodes);
	else OutNodes.Reset();
}

FWebToUEInstanceHandle UWebToUEView::GetFocusedSemanticNode() const
{
	return GetFocusedSemanticNodeForSlateUser(0);
}

bool UWebToUEView::RequestSemanticFocus(FWebToUEInstanceHandle Handle)
{
	return RequestSemanticFocusForSlateUser(Handle, 0);
}

bool UWebToUEView::ActivateSemanticNode(FWebToUEInstanceHandle Handle)
{
	return ActivateSemanticNodeForSlateUser(
		Handle, 0, EWebToUEInputModality::Unknown);
}

FWebToUEInstanceHandle UWebToUEView::GetFocusedSemanticNodeForSlateUser(
	uint32 SlateUserIndex) const
{
	return SlateView
		? SlateView->GetFocusedSemanticNode(SlateUserIndex) : FWebToUEInstanceHandle();
}

bool UWebToUEView::RequestSemanticFocusForSlateUser(
	FWebToUEInstanceHandle Handle, uint32 SlateUserIndex)
{
	if (!SlateView || !SlateView->RequestSemanticFocus(Handle, SlateUserIndex)) return false;
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetUserFocus(
			SlateUserIndex, SlateView, EFocusCause::SetDirectly);
	}
	return true;
}

bool UWebToUEView::ActivateSemanticNodeForSlateUser(
	FWebToUEInstanceHandle Handle,
	uint32 SlateUserIndex,
	EWebToUEInputModality InputModality)
{
	return SlateView && SlateView->ActivateSemanticNode(
		Handle, SlateUserIndex, InputModality);
}

#if WITH_DEV_AUTOMATION_TESTS
void UWebToUEView::LayoutForTesting(const FVector2f& ViewportSize)
{
	if (SlateView)
	{
		SlateView->LayoutForTesting(ViewportSize);
	}
}

bool UWebToUEView::GetRuntimeMemoryCensusForTesting(
	FWebToUERuntimeMemoryCensus& OutCensus) const
{
	return SlateView && SlateView->GetRuntimeMemoryCensusForTesting(OutCensus);
}

FWebToUENode* UWebToUEView::FindRuntimeNodeByIdForTesting(const FString& Id) const
{
	return SlateView ? SlateView->FindRuntimeNodeByIdForTesting(Id) : nullptr;
}

FText UWebToUEView::GetDisplayTextForTesting(const FWebToUENode& Node) const
{
	return SlateView ? SlateView->GetDisplayTextForTesting(Node) : FText::GetEmpty();
}

bool UWebToUEView::GetRuntimeVisibleForTesting(const FWebToUENode& Node) const
{
	return SlateView && SlateView->GetRuntimeStateForTesting(Node).bRuntimeVisible;
}

bool UWebToUEView::GetRuntimeEnabledForTesting(const FWebToUENode& Node) const
{
	return SlateView && SlateView->GetRuntimeStateForTesting(Node).bRuntimeEnabled;
}

EWebToUEPseudoState UWebToUEView::GetRuntimePseudoStatesForTesting(
	const FWebToUENode& Node) const
{
	return SlateView ? SlateView->GetRuntimeStateForTesting(Node).PseudoStates
		: EWebToUEPseudoState::None;
}

const FWebToUEComputedStyle& UWebToUEView::GetComputedStyleForTesting(
	const FWebToUENode& Node) const
{
	check(SlateView);
	return SlateView->GetComputedStyleForTesting(Node);
}

FWebToUEInstanceHandle UWebToUEView::GetInstanceHandleForTesting(
	const FWebToUENode& Node) const
{
	return SlateView ? SlateView->GetInstanceHandleForTesting(Node) : FWebToUEInstanceHandle();
}

FWebToUENode* UWebToUEView::ResolveInstanceHandleForTesting(
	FWebToUEInstanceHandle Handle) const
{
	return SlateView ? SlateView->ResolveInstanceHandleForTesting(Handle) : nullptr;
}

void UWebToUEView::SetHoveredNodeForTesting(FWebToUENode* Node)
{
	if (SlateView)
	{
		SlateView->SetHoveredNodeForTesting(Node);
	}
}

FReply UWebToUEView::OnMouseMoveForTesting(
	const FGeometry& Geometry, const FPointerEvent& PointerEvent)
{
	return SlateView
		? SlateView->OnMouseMove(Geometry, PointerEvent)
		: FReply::Unhandled();
}

const FWebToUERuntimeLayoutResult& UWebToUEView::GetLayoutResultForTesting(
	const FWebToUENode& Node) const
{
	check(SlateView);
	return SlateView->GetLayoutResultForTesting(Node);
}

#if WITH_EDITOR
void UWebToUEView::SetSafeZoneOverrideForTesting(
	FVector2D ScreenSize, float DPIScale)
{
	if (SafeZone)
	{
		SafeZone->SetOverrideScreenInformation(ScreenSize, DPIScale);
	}
}

FMargin UWebToUEView::GetSafeZoneMarginForTesting(float LayoutScale) const
{
	return SafeZone ? SafeZone->GetSafeMargin(LayoutScale) : FMargin();
}
#endif
#endif

void UWebToUEView::BindFieldNotifications()
{
	UnbindFieldNotifications();
	if (!DataContext || !SlateView) return;
	if (INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(DataContext))
	{
		const UE::FieldNotification::IClassDescriptor& Descriptor = Notify->GetFieldNotificationDescriptor();
		for (const FName FieldName : SlateView->GetBoundFields())
		{
			const UE::FieldNotification::FFieldId FieldId = Descriptor.GetField(DataContext->GetClass(), FieldName);
			if (FieldId.IsValid())
			{
				Notify->AddFieldValueChangedDelegate(FieldId,
					INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &UWebToUEView::HandleFieldValueChanged));
			}
		}
	}
}

void UWebToUEView::UnbindFieldNotifications()
{
	if (DataContext)
	{
		if (INotifyFieldValueChanged* Notify = Cast<INotifyFieldValueChanged>(DataContext))
		{
			Notify->RemoveAllFieldValueChangedDelegates(this);
		}
	}
}

void UWebToUEView::HandleFieldValueChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId)
{
	if (Object == DataContext && SlateView && FieldId.IsValid())
	{
		SlateView->RefreshBindings(DataContext, FieldId.GetName());
	}
}

void UWebToUEView::HandleDocumentChanged(UWebToUEDocument* ChangedDocument)
{
	if (ChangedDocument == Document && SlateView)
	{
		if (TSharedPtr<FWebToUESession> ActiveSession = Session.Pin())
		{
			ActiveSession->AdvanceGeneration();
		}
		SlateView->SetDocument(Document);
		SlateView->RefreshBindings(DataContext);
		BindFieldNotifications();
	}
}

#if WITH_EDITOR
const FText UWebToUEView::GetPaletteCategory()
{
	return NSLOCTEXT("WebToUE", "PaletteCategory", "WebToUE");
}
#endif
