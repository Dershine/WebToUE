#include "WebToUEView.h"

#include "SWebToUEView.h"
#include "WebToUEDocument.h"

UWebToUEView::UWebToUEView(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UWebToUEView::RebuildWidget()
{
	SlateView = SNew(SWebToUEView).Owner(this);
	if (!DocumentChangedHandle.IsValid())
	{
		DocumentChangedHandle = UWebToUEDocument::OnDocumentChanged().AddUObject(this, &UWebToUEView::HandleDocumentChanged);
	}
	SynchronizeProperties();
	return SlateView.ToSharedRef();
}

void UWebToUEView::SynchronizeProperties()
{
	Super::SynchronizeProperties();
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
	Document = InDocument;
	if (SlateView)
	{
		SlateView->SetDocument(Document);
		SlateView->RefreshBindings(DataContext);
		BindFieldNotifications();
	}
}

void UWebToUEView::SetDataContext(UObject* InDataContext)
{
	if (DataContext == InDataContext) return;
	UnbindFieldNotifications();
	DataContext = InDataContext;
	RefreshBindings();
	BindFieldNotifications();
}

void UWebToUEView::RefreshBindings()
{
	if (SlateView) SlateView->RefreshBindings(DataContext);
}

void UWebToUEView::HandleRuntimeEvent(FName EventName, FName ElementId)
{
	OnUIEvent.Broadcast(EventName, ElementId);
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
