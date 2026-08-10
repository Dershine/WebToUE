#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "INotifyFieldValueChanged.h"
#include "WebToUEView.generated.h"

class SWebToUEView;
class UWebToUEDocument;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWebToUEEvent, FName, EventName, FName, ElementId);

UCLASS(meta=(DisplayName="WebToUE View"))
class WEBTOUERUNTIME_API UWebToUEView : public UWidget
{
	GENERATED_BODY()

public:
	UWebToUEView(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE", meta=(ExposeOnSpawn=true))
	TObjectPtr<UWebToUEDocument> Document;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE", meta=(ExposeOnSpawn=true))
	TObjectPtr<UObject> DataContext;

	UPROPERTY(BlueprintAssignable, Category="WebToUE")
	FWebToUEEvent OnUIEvent;

	UFUNCTION(BlueprintCallable, Category="WebToUE")
	void SetDocument(UWebToUEDocument* InDocument);

	UFUNCTION(BlueprintCallable, Category="WebToUE")
	void SetDataContext(UObject* InDataContext);

	UFUNCTION(BlueprintCallable, Category="WebToUE")
	void RefreshBindings();

	void HandleRuntimeEvent(FName EventName, FName ElementId);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SWebToUEView> SlateView;
	FDelegateHandle DocumentChangedHandle;

	void BindFieldNotifications();
	void UnbindFieldNotifications();
	void HandleFieldValueChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);
	void HandleDocumentChanged(UWebToUEDocument* ChangedDocument);
};
