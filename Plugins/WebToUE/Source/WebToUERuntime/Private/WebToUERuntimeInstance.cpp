#include "WebToUERuntimeInstance.h"

#include "WebToUECompiler.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUEStyleProperties.h"

DEFINE_LOG_CATEGORY_STATIC(LogWebToUERuntimeInstance, Log, All);

namespace WebToUE::RuntimeInstance::Private
{
	static bool HydrateDeclaration(const FWebToUECompiledDeclaration& Source,
		FWebToUEStyleDeclaration& OutDeclaration)
	{
		if (Source.Property != EWebToUECssProperty::Invalid &&
			Source.TypedValue.Type != EWebToUEStyleValueType::Invalid)
		{
			OutDeclaration.Property = Source.Property;
			OutDeclaration.TypedValue = Source.TypedValue;
			return true;
		}
		return WebToUE::Private::TryParseCssDeclaration(Source.Name, Source.Value, OutDeclaration);
	}

	static bool HydrateLegacyInlineStyle(const FString& InlineStyle,
		TArray<FWebToUEStyleDeclaration>& OutDeclarations)
	{
		TArray<FString> Parts;
		InlineStyle.ParseIntoArray(Parts, TEXT(";"), true);
		for (const FString& Part : Parts)
		{
			FString Name;
			FString Value;
			if (!Part.Split(TEXT(":"), &Name, &Value)) return false;
			FWebToUEStyleDeclaration& Declaration = OutDeclarations.AddDefaulted_GetRef();
			if (!WebToUE::Private::TryParseCssDeclaration(Name, Value, Declaration)) return false;
		}
		return true;
	}
}

void FWebToUERuntimeInstance::Reset()
{
	RuntimeDocument.Reset();
	HoveredNode = nullptr;
	PressedNode = nullptr;
	FocusedNode = nullptr;
}

bool FWebToUERuntimeInstance::Hydrate(const UWebToUEDocument& CompiledDocument)
{
	Reset();
	const TArray<FWebToUECompiledNode>& CompiledNodes = CompiledDocument.GetCompiledNodes();
	if (!CompiledNodes.IsValidIndex(CompiledDocument.GetRootNodeIndex()))
	{
		return false;
	}

	const TArray<FWebToUECompiledRule>& CompiledRules = CompiledDocument.GetCompiledRules();
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::HydratedNodes, CompiledNodes.Num());
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::HydratedRules, CompiledRules.Num());
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
	RuntimeDocument = MakeShared<FWebToUEDocument>();

	TArray<TSharedPtr<FWebToUENode>> Nodes;
	if (!CompiledNodes.IsEmpty())
	{
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
	}
	Nodes.Reserve(CompiledNodes.Num());
	for (const FWebToUECompiledNode& Source : CompiledNodes)
	{
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		TSharedPtr<FWebToUENode> Node = MakeShared<FWebToUENode>();
		Node->Type = static_cast<EWebToUENodeType>(Source.Type);
		Node->Tag = Source.Tag;
		Node->Text = Source.Text;
		Node->LocalizedText = Source.LocalizedText;
		Node->bHasLocalizedText = Node->Type == EWebToUENodeType::Text;
		Node->bRichText = Source.bRichText;
		for (const FWebToUECompiledAttribute& Attribute : Source.Attributes)
		{
			Node->Attributes.Add(Attribute.Name, Attribute.Value);
		}
		for (const FWebToUECompiledDeclaration& SourceDeclaration : Source.InlineStyleDeclarations)
		{
			FWebToUEStyleDeclaration& RuntimeDeclaration = Node->InlineStyleDeclarations.AddDefaulted_GetRef();
			if (!WebToUE::RuntimeInstance::Private::HydrateDeclaration(SourceDeclaration, RuntimeDeclaration))
			{
				UE_LOG(LogWebToUERuntimeInstance, Error,
					TEXT("Failed to hydrate a typed inline style declaration for node '%s'."), *Source.Tag);
				Reset();
				return false;
			}
		}
		if (Source.InlineStyleDeclarations.IsEmpty())
		{
			const FString* LegacyInlineStyle = Node->Attributes.Find(TEXT("style"));
			if (LegacyInlineStyle && !LegacyInlineStyle->IsEmpty() &&
				!WebToUE::RuntimeInstance::Private::HydrateLegacyInlineStyle(
					*LegacyInlineStyle, Node->InlineStyleDeclarations))
			{
				UE_LOG(LogWebToUERuntimeInstance, Error,
					TEXT("Failed to hydrate legacy inline style declarations for node '%s'."), *Source.Tag);
				Reset();
				return false;
			}
		}
		Nodes.Add(MoveTemp(Node));
	}
	for (int32 Index = 0; Index < CompiledNodes.Num(); ++Index)
	{
		const int32 ParentIndex = CompiledNodes[Index].ParentIndex;
		if (Nodes.IsValidIndex(ParentIndex))
		{
			Nodes[Index]->Parent = Nodes[ParentIndex].Get();
			Nodes[ParentIndex]->Children.Add(Nodes[Index]);
		}
	}
	RuntimeDocument->Root = Nodes[CompiledDocument.GetRootNodeIndex()];

	for (const FWebToUECompiledRule& SourceRule : CompiledRules)
	{
		FWebToUEStyleRule Rule;
		Rule.Specificity = SourceRule.Specificity;
		Rule.SourceOrder = SourceRule.SourceOrder;
		for (const FWebToUECompiledSelectorSegment& SourceSegment : SourceRule.Selector)
		{
			FWebToUESelectorSegment Segment;
			Segment.Type = SourceSegment.Type;
			Segment.Id = SourceSegment.Id;
			Segment.Classes = SourceSegment.Classes;
			Segment.RequiredState = static_cast<EWebToUEPseudoState>(SourceSegment.RequiredState);
			Segment.RelationToPrevious = static_cast<EWebToUECombinator>(SourceSegment.RelationToPrevious);
			Rule.Selector.Add(MoveTemp(Segment));
		}
		for (const FWebToUECompiledDeclaration& Declaration : SourceRule.Declarations)
		{
			FWebToUEStyleDeclaration& RuntimeDeclaration = Rule.Declarations.AddDefaulted_GetRef();
			if (!WebToUE::RuntimeInstance::Private::HydrateDeclaration(Declaration, RuntimeDeclaration))
			{
				UE_LOG(LogWebToUERuntimeInstance, Error,
					TEXT("Failed to hydrate legacy or typed style declaration '%s'."), *Declaration.Name);
				Reset();
				return false;
			}
		}
		RuntimeDocument->Rules.Add(MoveTemp(Rule));
	}

	RuntimeDocument->InitializeRuntimeData();
	FWebToUEStyleResolver::Resolve(*RuntimeDocument);
	return true;
}
