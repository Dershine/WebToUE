#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "INotifyFieldValueChanged.h"
#include "WebToUEIdentity.h"
#include "WebToUESemantics.h"
#include "WebToUEView.generated.h"

class SWebToUEView;
class SSafeZone;
class FWebToUESession;
class UWebToUEDocument;
struct FWebToUERuntimeLayoutResult;
enum class EWebToUEPseudoState : uint8;
enum class EWebToUEInputModality : uint8;
struct FWebToUEComputedStyle;
struct FWebToUENode;

#if WITH_DEV_AUTOMATION_TESTS
struct FWebToUERuntimeMemoryCensus
{
	uint64 SharedStyleTemplateKnownOwnedBytes = 0;
	uint64 RuntimeKnownOwnedBytes = 0;
	uint64 PresentationKnownOwnedBytes = 0;
	int32 RuntimeNodeCount = 0;
	int32 RuntimeRuleCount = 0;

	uint64 GetTotalKnownOwnedBytes() const
	{
		return RuntimeKnownOwnedBytes + PresentationKnownOwnedBytes;
	}
};
#endif

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWebToUEEvent, FName, EventName, FName, ElementId);

UCLASS(meta=(DisplayName="WebToUE View"))
class WEBTOUERUNTIME_API UWebToUEView : public UWidget, public IWebToUESemanticFocusSource
{
	GENERATED_BODY()

public:
	UWebToUEView(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE", meta=(ExposeOnSpawn=true))
	TObjectPtr<UWebToUEDocument> Document;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE", meta=(ExposeOnSpawn=true))
	TObjectPtr<UObject> DataContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE", meta=(ExposeOnSpawn=true))
	bool bRespectSafeZone = true;

	UPROPERTY(BlueprintAssignable, Category="WebToUE")
	FWebToUEEvent OnUIEvent;

	UFUNCTION(BlueprintCallable, Category="WebToUE")
	void SetDocument(UWebToUEDocument* InDocument);

	UFUNCTION(BlueprintCallable, Category="WebToUE")
	void SetDataContext(UObject* InDataContext);

	UFUNCTION(BlueprintCallable, Category="WebToUE")
	void RefreshBindings();

	UFUNCTION(BlueprintCallable, Category="WebToUE")
	void SetRespectSafeZone(bool bInRespectSafeZone);

	void SetSession(TSharedPtr<FWebToUESession> InSession);
	void ClearSession();
	TSharedPtr<FWebToUESession> GetSession() const { return Session.Pin(); }

	void HandleRuntimeEvent(FName EventName, FName ElementId);
	virtual void GetSemanticNodes(TArray<FWebToUESemanticNode>& OutNodes) const override;
	virtual FWebToUEInstanceHandle GetFocusedSemanticNode() const override;
	virtual bool RequestSemanticFocus(FWebToUEInstanceHandle Handle) override;
	virtual bool ActivateSemanticNode(FWebToUEInstanceHandle Handle) override;
	FWebToUEInstanceHandle GetFocusedSemanticNodeForSlateUser(uint32 SlateUserIndex) const;
	bool RequestSemanticFocusForSlateUser(
		FWebToUEInstanceHandle Handle, uint32 SlateUserIndex);
	bool ActivateSemanticNodeForSlateUser(
		FWebToUEInstanceHandle Handle,
		uint32 SlateUserIndex,
		EWebToUEInputModality InputModality);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_DEV_AUTOMATION_TESTS
	void LayoutForTesting(const FVector2f& ViewportSize);
	bool GetRuntimeMemoryCensusForTesting(FWebToUERuntimeMemoryCensus& OutCensus) const;
	FWebToUENode* FindRuntimeNodeByIdForTesting(const FString& Id) const;
	FText GetDisplayTextForTesting(const FWebToUENode& Node) const;
	bool GetRuntimeVisibleForTesting(const FWebToUENode& Node) const;
	bool GetRuntimeEnabledForTesting(const FWebToUENode& Node) const;
	EWebToUEPseudoState GetRuntimePseudoStatesForTesting(const FWebToUENode& Node) const;
	const FWebToUEComputedStyle& GetComputedStyleForTesting(const FWebToUENode& Node) const;
	FWebToUEInstanceHandle GetInstanceHandleForTesting(const FWebToUENode& Node) const;
	FWebToUENode* ResolveInstanceHandleForTesting(FWebToUEInstanceHandle Handle) const;
	void SetHoveredNodeForTesting(FWebToUENode* Node);
	FReply OnMouseMoveForTesting(
		const FGeometry& Geometry, const FPointerEvent& PointerEvent);
	const FWebToUERuntimeLayoutResult& GetLayoutResultForTesting(
		const FWebToUENode& Node) const;
	TSharedPtr<SWebToUEView> GetSlateViewForTesting() const { return SlateView; }
#if WITH_EDITOR
	void SetSafeZoneOverrideForTesting(FVector2D ScreenSize, float DPIScale);
	FMargin GetSafeZoneMarginForTesting(float LayoutScale) const;
	TSharedPtr<SSafeZone> GetSafeZoneForTesting() const { return SafeZone; }
#endif
#endif

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SWebToUEView> SlateView;
	TSharedPtr<SSafeZone> SafeZone;
	FDelegateHandle DocumentChangedHandle;
	TWeakPtr<FWebToUESession> Session;

	void ApplySafeZoneSetting();
	void BindFieldNotifications();
	void UnbindFieldNotifications();
	void HandleFieldValueChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);
	void HandleDocumentChanged(UWebToUEDocument* ChangedDocument);
};
