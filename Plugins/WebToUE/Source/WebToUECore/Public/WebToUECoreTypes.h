#pragma once

#include "CoreMinimal.h"
#include "WebToUECoreTypes.generated.h"

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
	Hidden,
	Auto,
	Scroll
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

UENUM()
enum class EWebToUEUnit : uint8
{
	Auto,
	Pixels,
	Percent,
	Undefined
};

USTRUCT()
struct WEBTOUECORE_API FWebToUELength
{
	GENERATED_BODY()

	UPROPERTY() EWebToUEUnit Unit = EWebToUEUnit::Undefined;
	UPROPERTY() float Value = 0.0f;

	static FWebToUELength Auto() { FWebToUELength Result; Result.Unit = EWebToUEUnit::Auto; return Result; }
	static FWebToUELength Pixels(float InValue) { FWebToUELength Result; Result.Unit = EWebToUEUnit::Pixels; Result.Value = InValue; return Result; }
	static FWebToUELength Percent(float InValue) { FWebToUELength Result; Result.Unit = EWebToUEUnit::Percent; Result.Value = InValue; return Result; }
	bool IsDefined() const { return Unit != EWebToUEUnit::Undefined; }
};

USTRUCT()
struct WEBTOUECORE_API FWebToUEEdges
{
	GENERATED_BODY()

	UPROPERTY() FWebToUELength Left;
	UPROPERTY() FWebToUELength Top;
	UPROPERTY() FWebToUELength Right;
	UPROPERTY() FWebToUELength Bottom;
};

// Serialized values are append-only. Changing an existing numeric value requires an asset version migration.
UENUM()
enum class EWebToUECssProperty : uint8
{
	Invalid = 0,
	Display,
	Position,
	Visibility,
	Overflow,
	Width,
	Height,
	MinWidth,
	MinHeight,
	MaxWidth,
	MaxHeight,
	Left,
	Top,
	Right,
	Bottom,
	Margin,
	MarginLeft,
	MarginTop,
	MarginRight,
	MarginBottom,
	Padding,
	PaddingLeft,
	PaddingTop,
	PaddingRight,
	PaddingBottom,
	Gap,
	RowGap,
	ColumnGap,
	Flex,
	FlexDirection,
	FlexWrap,
	FlexGrow,
	FlexShrink,
	FlexBasis,
	JustifyContent,
	AlignItems,
	AlignSelf,
	Color,
	Background,
	BackgroundColor,
	Border,
	BorderColor,
	BorderWidth,
	BorderStyle,
	BorderRadius,
	Opacity,
	FontFamily,
	FontSize,
	FontWeight,
	TextAlign,
	WhiteSpace,
	ObjectFit,
	ZIndex
};

UENUM()
enum class EWebToUEStyleValueType : uint8
{
	Invalid = 0,
	Keyword,
	Number,
	Integer,
	Length,
	Edges,
	Color,
	String,
	Flex,
	Border
};

UENUM()
enum class EWebToUEStyleKeyword : uint8
{
	None = 0,
	Flex,
	Relative,
	Absolute,
	Visible,
	Hidden,
	Auto,
	Scroll,
	Row,
	RowReverse,
	Column,
	ColumnReverse,
	NoWrap,
	Wrap,
	WrapReverse,
	FlexStart,
	Center,
	FlexEnd,
	SpaceBetween,
	SpaceAround,
	SpaceEvenly,
	Stretch,
	Baseline,
	Solid,
	Left,
	Right,
	Normal,
	Fill,
	Contain,
	Cover
};

USTRUCT()
struct WEBTOUECORE_API FWebToUEFlexValue
{
	GENERATED_BODY()

	UPROPERTY() float Grow = 0.0f;
	UPROPERTY() float Shrink = 1.0f;
	UPROPERTY() FWebToUELength Basis = FWebToUELength::Auto();
	UPROPERTY() bool bHasGrow = false;
	UPROPERTY() bool bHasShrink = false;
	UPROPERTY() bool bHasBasis = false;
};

USTRUCT()
struct WEBTOUECORE_API FWebToUEBorderValue
{
	GENERATED_BODY()

	UPROPERTY() float Width = 0.0f;
	UPROPERTY() FLinearColor Color = FLinearColor::Transparent;
	UPROPERTY() bool bHasWidth = false;
	UPROPERTY() bool bHasColor = false;
};

USTRUCT()
struct WEBTOUECORE_API FWebToUEStyleValue
{
	GENERATED_BODY()

	UPROPERTY() EWebToUEStyleValueType Type = EWebToUEStyleValueType::Invalid;
	UPROPERTY() EWebToUEStyleKeyword Keyword = EWebToUEStyleKeyword::None;
	UPROPERTY() float Number = 0.0f;
	UPROPERTY() int32 Integer = 0;
	UPROPERTY() FWebToUELength Length;
	UPROPERTY() FWebToUEEdges Edges;
	UPROPERTY() FLinearColor Color = FLinearColor::Transparent;
	UPROPERTY() FString String;
	UPROPERTY() FWebToUEFlexValue Flex;
	UPROPERTY() FWebToUEBorderValue Border;
};

struct WEBTOUECORE_API FWebToUEStyleDeclaration
{
	EWebToUECssProperty Property = EWebToUECssProperty::Invalid;
	FWebToUEStyleValue TypedValue;
	// Authoring-only source spelling. Compiled assets serialize Property and TypedValue instead.
	FString Name;
	FString Value;
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

struct WEBTOUECORE_API FWebToUERuntimeNodeState
{
	EWebToUEPseudoState PseudoStates = EWebToUEPseudoState::None;
	bool bRuntimeVisible = true;
	bool bRuntimeEnabled = true;
	bool bHasBoundText = false;
	FText BoundText;
	bool bHasRichTextOverride = false;
	bool bRichTextOverride = false;
	FVector2f ScrollOffset = FVector2f::ZeroVector;
	FVector2f MaxScrollOffset = FVector2f::ZeroVector;
};

struct WEBTOUECORE_API FWebToUERuntimeLayoutResult
{
	FVector2f Position = FVector2f::ZeroVector;
	FVector2f Size = FVector2f::ZeroVector;
	int32 PaintOrder = 0;
};

struct WEBTOUECORE_API FWebToUERuntimeRenderData
{
	FWebToUEComputedStyle ComputedStyle;
	FWebToUERuntimeLayoutResult LayoutResult;
};

struct WEBTOUECORE_API FWebToUENode : public TSharedFromThis<FWebToUENode>
{
	EWebToUENodeType Type = EWebToUENodeType::Element;
	FString Tag;
	FString Text;
	FText LocalizedText;
	bool bHasLocalizedText = false;
	bool bRichText = false;
	bool bTextHadLeadingWhitespace = false;
	bool bTextHadTrailingWhitespace = false;
	TMap<FString, FString> Attributes;
	TArray<FWebToUEStyleDeclaration> InlineStyleDeclarations;
	TArray<TSharedPtr<FWebToUENode>> Children;
	FWebToUENode* Parent = nullptr;
	int32 RuntimeDataIndex = INDEX_NONE;

	FString GetAttribute(const FString& Name) const;
	bool HasClass(const FString& ClassName) const;
	bool IsInteractive() const;
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
	TArray<FWebToUEStyleDeclaration> Declarations;
	int32 Specificity = 0;
	int32 SourceOrder = 0;
};

struct WEBTOUECORE_API FWebToUEDocument
{
	TSharedPtr<FWebToUENode> Root;
	TArray<FWebToUEStyleRule> Rules;
	TArray<FString> LinkedStylesheets;
	TArray<FWebToUEDiagnostic> Diagnostics;
	TArray<FWebToUERuntimeNodeState> RuntimeNodeStates;
	TArray<FWebToUERuntimeRenderData> RuntimeRenderData;

	bool HasErrors() const;
	void InitializeRuntimeData();
	void AddRuntimeNodeData(FWebToUENode& Node);
	FORCEINLINE bool IsValidRuntimeNodeStateIndex(const FWebToUENode& Node) const
	{
		return RuntimeNodeStates.IsValidIndex(Node.RuntimeDataIndex);
	}
	FORCEINLINE FWebToUERuntimeNodeState& GetRuntimeNodeState(FWebToUENode& Node)
	{
		return RuntimeNodeStates[Node.RuntimeDataIndex];
	}
	FORCEINLINE const FWebToUERuntimeNodeState& GetRuntimeNodeState(const FWebToUENode& Node) const
	{
		return RuntimeNodeStates[Node.RuntimeDataIndex];
	}
	FORCEINLINE FWebToUEComputedStyle& GetComputedStyle(FWebToUENode& Node)
	{
		return RuntimeRenderData[Node.RuntimeDataIndex].ComputedStyle;
	}
	FORCEINLINE const FWebToUEComputedStyle& GetComputedStyle(const FWebToUENode& Node) const
	{
		return RuntimeRenderData[Node.RuntimeDataIndex].ComputedStyle;
	}
	FORCEINLINE FWebToUERuntimeLayoutResult& GetLayoutResult(FWebToUENode& Node)
	{
		return RuntimeRenderData[Node.RuntimeDataIndex].LayoutResult;
	}
	FORCEINLINE const FWebToUERuntimeLayoutResult& GetLayoutResult(const FWebToUENode& Node) const
	{
		return RuntimeRenderData[Node.RuntimeDataIndex].LayoutResult;
	}
	bool IsDisplayed(const FWebToUENode& Node) const;
	bool ClipsOverflow(const FWebToUENode& Node) const;
	bool IsScrollable(const FWebToUENode& Node) const;
	void ForEachNode(TFunctionRef<void(FWebToUENode&)> Visitor) const;
};
