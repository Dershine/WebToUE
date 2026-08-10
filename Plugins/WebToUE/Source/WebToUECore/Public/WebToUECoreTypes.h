#pragma once

#include "CoreMinimal.h"

enum class EWebToUEDiagnosticSeverity : uint8
{
	Info,
	Warning,
	Error
};

struct WEBTOUECORE_API FWebToUEDiagnostic
{
	EWebToUEDiagnosticSeverity Severity = EWebToUEDiagnosticSeverity::Info;
	FString File;
	int32 Line = 1;
	int32 Column = 1;
	FString Message;
};

enum class EWebToUENodeType : uint8
{
	Element,
	Text
};

enum class EWebToUEDisplay : uint8
{
	Flex,
	None
};

enum class EWebToUEPosition : uint8
{
	Relative,
	Absolute
};

enum class EWebToUEOverflow : uint8
{
	Visible,
	Hidden
};

enum class EWebToUEFlexDirection : uint8
{
	Row,
	RowReverse,
	Column,
	ColumnReverse
};

enum class EWebToUEPseudoState : uint8
{
	None = 0,
	Hover = 1 << 0,
	Active = 1 << 1,
	Focus = 1 << 2,
	Disabled = 1 << 3
};
ENUM_CLASS_FLAGS(EWebToUEPseudoState)

enum class EWebToUEUnit : uint8
{
	Auto,
	Pixels,
	Percent,
	Undefined
};

struct WEBTOUECORE_API FWebToUELength
{
	EWebToUEUnit Unit = EWebToUEUnit::Undefined;
	float Value = 0.0f;

	static FWebToUELength Auto() { return { EWebToUEUnit::Auto, 0.0f }; }
	static FWebToUELength Pixels(float InValue) { return { EWebToUEUnit::Pixels, InValue }; }
	static FWebToUELength Percent(float InValue) { return { EWebToUEUnit::Percent, InValue }; }
	bool IsDefined() const { return Unit != EWebToUEUnit::Undefined; }
};

struct WEBTOUECORE_API FWebToUEEdges
{
	FWebToUELength Left;
	FWebToUELength Top;
	FWebToUELength Right;
	FWebToUELength Bottom;
};

struct WEBTOUECORE_API FWebToUEComputedStyle
{
	EWebToUEDisplay Display = EWebToUEDisplay::Flex;
	EWebToUEPosition Position = EWebToUEPosition::Relative;
	EWebToUEOverflow Overflow = EWebToUEOverflow::Visible;
	EWebToUEFlexDirection FlexDirection = EWebToUEFlexDirection::Column;
	FString FlexWrap = TEXT("nowrap");
	FString JustifyContent = TEXT("flex-start");
	FString AlignItems = TEXT("stretch");
	FString AlignSelf = TEXT("auto");
	float FlexGrow = 0.0f;
	float FlexShrink = 1.0f;
	FWebToUELength FlexBasis = FWebToUELength::Auto();
	FWebToUELength Width;
	FWebToUELength Height;
	FWebToUELength MinWidth;
	FWebToUELength MinHeight;
	FWebToUELength MaxWidth;
	FWebToUELength MaxHeight;
	FWebToUEEdges Margin;
	FWebToUEEdges Padding;
	FWebToUEEdges Inset;
	float RowGap = 0.0f;
	float ColumnGap = 0.0f;
	FLinearColor Color = FLinearColor::White;
	FLinearColor BackgroundColor = FLinearColor::Transparent;
	FLinearColor BorderColor = FLinearColor::Transparent;
	float BorderWidth = 0.0f;
	float BorderRadius = 0.0f;
	float Opacity = 1.0f;
	bool bVisible = true;
	bool bEnabled = true;
	FString FontFamily = TEXT("Default");
	float FontSize = 16.0f;
	FString FontWeight = TEXT("normal");
	FString TextAlign = TEXT("left");
	FString WhiteSpace = TEXT("normal");
	FString ObjectFit = TEXT("fill");
	int32 ZIndex = 0;
};

struct WEBTOUECORE_API FWebToUENode : public TSharedFromThis<FWebToUENode>
{
	EWebToUENodeType Type = EWebToUENodeType::Element;
	FString Tag;
	FString Text;
	TMap<FString, FString> Attributes;
	TArray<TSharedPtr<FWebToUENode>> Children;
	FWebToUENode* Parent = nullptr;
	EWebToUEPseudoState StateFlags = EWebToUEPseudoState::None;
	FWebToUEComputedStyle Style;
	bool bRuntimeVisible = true;
	bool bRuntimeEnabled = true;
	FVector2f Position = FVector2f::ZeroVector;
	FVector2f Size = FVector2f::ZeroVector;
	int32 PaintOrder = 0;

	FString GetAttribute(const FString& Name) const;
	bool HasClass(const FString& ClassName) const;
	bool IsInteractive() const;
	bool IsDisplayed() const;
};

enum class EWebToUECombinator : uint8
{
	None,
	Descendant,
	Child
};

struct WEBTOUECORE_API FWebToUESelectorSegment
{
	FString Type;
	FString Id;
	TArray<FString> Classes;
	EWebToUEPseudoState RequiredState = EWebToUEPseudoState::None;
	EWebToUECombinator RelationToPrevious = EWebToUECombinator::None;
};

struct WEBTOUECORE_API FWebToUEStyleRule
{
	TArray<FWebToUESelectorSegment> Selector;
	TMap<FString, FString> Declarations;
	int32 Specificity = 0;
	int32 SourceOrder = 0;
};

struct WEBTOUECORE_API FWebToUEDocument
{
	TSharedPtr<FWebToUENode> Root;
	TArray<FWebToUEStyleRule> Rules;
	TArray<FString> LinkedStylesheets;
	TArray<FWebToUEDiagnostic> Diagnostics;

	bool HasErrors() const;
	void ForEachNode(TFunctionRef<void(FWebToUENode&)> Visitor) const;
};
