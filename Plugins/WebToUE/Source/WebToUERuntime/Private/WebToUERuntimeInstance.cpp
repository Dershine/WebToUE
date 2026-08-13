#include "WebToUERuntimeInstance.h"

#include "WebToUECompiler.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUEStyleProperties.h"

DEFINE_LOG_CATEGORY_STATIC(LogWebToUERuntimeInstance, Log, All);

namespace WebToUE::RuntimeInstance::Private
{
	static uint64 GetStyleValueOwnedBytes(const FWebToUEStyleValue& Value)
	{
		return Value.String.GetAllocatedSize();
	}

	static uint64 GetDeclarationOwnedBytes(const FWebToUEStyleDeclaration& Declaration)
	{
		return GetStyleValueOwnedBytes(Declaration.TypedValue) +
			Declaration.Name.GetAllocatedSize() + Declaration.Value.GetAllocatedSize();
	}

	static uint64 GetComputedStyleOwnedBytes(const FWebToUEComputedStyle& Style)
	{
		return Style.FlexWrap.GetAllocatedSize() + Style.JustifyContent.GetAllocatedSize() +
			Style.AlignItems.GetAllocatedSize() + Style.AlignSelf.GetAllocatedSize() +
			Style.FontFamily.GetAllocatedSize() + Style.FontWeight.GetAllocatedSize() +
			Style.TextAlign.GetAllocatedSize() + Style.WhiteSpace.GetAllocatedSize() +
			Style.ObjectFit.GetAllocatedSize();
	}

	static uint64 GetSelectorIndexOwnedBytes(const FWebToUESelectorIndex& Index)
	{
		uint64 Bytes = Index.IdRules.GetAllocatedSize() + Index.ClassRules.GetAllocatedSize() +
			Index.TagRules.GetAllocatedSize() + Index.HoverRules.GetAllocatedSize() +
			Index.ActiveRules.GetAllocatedSize() + Index.FocusRules.GetAllocatedSize() +
			Index.DisabledRules.GetAllocatedSize() + Index.UniversalRules.GetAllocatedSize();
		const auto AddMapBytes = [&Bytes](const TMap<FString, TArray<int32>>& Map)
		{
			for (const TPair<FString, TArray<int32>>& Pair : Map)
			{
				Bytes += Pair.Key.GetAllocatedSize() + Pair.Value.GetAllocatedSize();
			}
		};
		AddMapBytes(Index.IdRules);
		AddMapBytes(Index.ClassRules);
		AddMapBytes(Index.TagRules);
		return Bytes;
	}

	static uint64 GetStyleTemplateOwnedBytes(const FWebToUERuntimeStyleTemplate& StyleTemplate)
	{
		uint64 Bytes = sizeof(StyleTemplate) + StyleTemplate.Rules.GetAllocatedSize() +
			GetSelectorIndexOwnedBytes(StyleTemplate.SelectorIndex) +
			StyleTemplate.PseudoInvalidationDependencies.GetAllocatedSize();
		for (const FWebToUEStyleRule& Rule : StyleTemplate.Rules)
		{
			Bytes += Rule.Selector.GetAllocatedSize() + Rule.Declarations.GetAllocatedSize();
			for (const FWebToUESelectorSegment& Segment : Rule.Selector)
			{
				Bytes += Segment.Type.GetAllocatedSize() + Segment.Id.GetAllocatedSize() +
					Segment.Classes.GetAllocatedSize();
				for (const FString& ClassName : Segment.Classes)
				{
					Bytes += ClassName.GetAllocatedSize();
				}
			}
			for (const FWebToUEStyleDeclaration& Declaration : Rule.Declarations)
			{
				Bytes += GetDeclarationOwnedBytes(Declaration);
			}
		}
		return Bytes;
	}

	static uint64 GetSelectorTargetIndexOwnedBytes(
		const FWebToUERuntimeSelectorTargetIndex& Index)
	{
		uint64 Bytes = Index.IdTargets.GetAllocatedSize() +
			Index.ClassTargets.GetAllocatedSize() + Index.TagTargets.GetAllocatedSize() +
			Index.UniversalTargets.GetAllocatedSize();
		const auto AddMapBytes = [&Bytes](
			const TMap<FString, TArray<FWebToUEInstanceHandle>>& Map)
		{
			for (const TPair<FString, TArray<FWebToUEInstanceHandle>>& Pair : Map)
			{
				Bytes += Pair.Key.GetAllocatedSize() + Pair.Value.GetAllocatedSize();
			}
		};
		AddMapBytes(Index.IdTargets);
		AddMapBytes(Index.ClassTargets);
		AddMapBytes(Index.TagTargets);
		return Bytes;
	}

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

#if WITH_DEV_AUTOMATION_TESTS
uint64 FWebToUERuntimeInstance::GetKnownOwnedBytesForTesting() const
{
	using namespace WebToUE::RuntimeInstance::Private;
	uint64 Bytes = sizeof(*this);
	if (!RuntimeDocument)
	{
		return Bytes;
	}

	Bytes += sizeof(FWebToUEDocument);
	Bytes += RuntimeDocument->Rules.GetAllocatedSize();
	Bytes += RuntimeDocument->LinkedStylesheets.GetAllocatedSize();
	Bytes += RuntimeDocument->Diagnostics.GetAllocatedSize();
	Bytes += RuntimeDocument->RuntimeNodeStates.GetAllocatedSize();
	Bytes += RuntimeDocument->RuntimeRenderData.GetAllocatedSize();
	Bytes += RuntimeDocument->RuntimeNodesBySlot.GetAllocatedSize();
	Bytes += RuntimeDocument->PseudoInvalidationDependencies.GetAllocatedSize();
	Bytes += GetSelectorIndexOwnedBytes(RuntimeDocument->SelectorIndex);
	Bytes += GetSelectorTargetIndexOwnedBytes(RuntimeDocument->RuntimeSelectorTargets);

	for (const FString& Stylesheet : RuntimeDocument->LinkedStylesheets)
	{
		Bytes += Stylesheet.GetAllocatedSize();
	}
	for (const FWebToUEStyleRule& Rule : RuntimeDocument->Rules)
	{
		Bytes += Rule.Selector.GetAllocatedSize() + Rule.Declarations.GetAllocatedSize();
		for (const FWebToUESelectorSegment& Segment : Rule.Selector)
		{
			Bytes += Segment.Type.GetAllocatedSize() + Segment.Id.GetAllocatedSize() +
				Segment.Classes.GetAllocatedSize();
			for (const FString& ClassName : Segment.Classes)
			{
				Bytes += ClassName.GetAllocatedSize();
			}
		}
		for (const FWebToUEStyleDeclaration& Declaration : Rule.Declarations)
		{
			Bytes += GetDeclarationOwnedBytes(Declaration);
		}
	}

	RuntimeDocument->ForEachNode([&Bytes](FWebToUENode& Node)
	{
		Bytes += sizeof(FWebToUENode) + Node.Tag.GetAllocatedSize() + Node.Text.GetAllocatedSize() +
			Node.Attributes.GetAllocatedSize() + Node.InlineStyleDeclarations.GetAllocatedSize() +
			Node.Children.GetAllocatedSize() + Node.SelectorId.GetAllocatedSize() +
			Node.SelectorClasses.GetAllocatedSize();
		for (const TPair<FString, FString>& Attribute : Node.Attributes)
		{
			Bytes += Attribute.Key.GetAllocatedSize() + Attribute.Value.GetAllocatedSize();
		}
		for (const FWebToUEStyleDeclaration& Declaration : Node.InlineStyleDeclarations)
		{
			Bytes += GetDeclarationOwnedBytes(Declaration);
		}
		for (const FString& ClassName : Node.SelectorClasses)
		{
			Bytes += ClassName.GetAllocatedSize();
		}
	});
	for (const FWebToUERuntimeRenderData& RenderData : RuntimeDocument->RuntimeRenderData)
	{
		Bytes += GetComputedStyleOwnedBytes(RenderData.ComputedStyle);
	}
	return Bytes;
}

uint64 FWebToUERuntimeInstance::GetSharedStyleTemplateKnownOwnedBytesForTesting() const
{
	if (!RuntimeDocument || !RuntimeDocument->SharedStyleTemplate)
	{
		return 0;
	}
	return WebToUE::RuntimeInstance::Private::GetStyleTemplateOwnedBytes(
		*RuntimeDocument->SharedStyleTemplate);
}

const void* FWebToUERuntimeInstance::GetSharedStyleTemplateIdentityForTesting() const
{
	return RuntimeDocument ? RuntimeDocument->SharedStyleTemplate.Get() : nullptr;
}

int32 FWebToUERuntimeInstance::GetRuntimeNodeCountForTesting() const
{
	int32 NodeCount = 0;
	if (RuntimeDocument)
	{
		RuntimeDocument->ForEachNode([&NodeCount](FWebToUENode&) { ++NodeCount; });
	}
	return NodeCount;
}

int32 FWebToUERuntimeInstance::GetRuntimeRuleCountForTesting() const
{
	return RuntimeDocument ? RuntimeDocument->GetRules().Num() : 0;
}
#endif

FWebToUERuntimeInstance::FWebToUERuntimeInstance()
	: OwnerId(AllocateWebToUEInstanceOwnerId())
	, LayoutEngine(MakeUnique<FWebToUELayoutEngine>())
{
}

FWebToUERuntimeInstance::~FWebToUERuntimeInstance() = default;

void FWebToUERuntimeInstance::AdoptDocumentForTesting(TSharedRef<FWebToUEDocument> InDocument)
{
	Reset();
	RuntimeDocument = MoveTemp(InDocument);
	RuntimeDocument->InitializeRuntimeData(OwnerId, Generation);
}

FWebToUEInstanceHandle FWebToUERuntimeInstance::GetHandle(const FWebToUENode* Node) const
{
	if (!Node || !RuntimeDocument || RuntimeDocument->ResolveNode(Node->InstanceHandle) != Node)
	{
		return {};
	}
	return Node->InstanceHandle;
}

FWebToUENode* FWebToUERuntimeInstance::ResolveNode(FWebToUEInstanceHandle Handle)
{
	return RuntimeDocument ? RuntimeDocument->ResolveNode(Handle) : nullptr;
}

const FWebToUENode* FWebToUERuntimeInstance::ResolveNode(FWebToUEInstanceHandle Handle) const
{
	return RuntimeDocument ? RuntimeDocument->ResolveNode(Handle) : nullptr;
}

TConstArrayView<FWebToUERuntimeBindingOp> FWebToUERuntimeInstance::GetBindingOps(
	FName RootField) const
{
	if (const TArray<FWebToUERuntimeBindingOp>* Ops = BindingOpsByField.Find(RootField))
	{
		return *Ops;
	}
	return {};
}

void FWebToUERuntimeInstance::Reset()
{
	LayoutEngine->Reset();
	++Generation;
	if (Generation == 0)
	{
		++Generation;
	}
	RuntimeDocument.Reset();
	HoveredNode = {};
	PressedNode = {};
	FocusedNode = {};
	BindingOpsByField.Reset();
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
	const TSharedPtr<const FWebToUERuntimeStyleTemplate> SharedStyleTemplate =
		CompiledDocument.GetOrCreateRuntimeStyleTemplate();
	if (!SharedStyleTemplate)
	{
		return false;
	}
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::HydratedNodes, CompiledNodes.Num());
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::HydratedRules, CompiledRules.Num());
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
	RuntimeDocument = MakeShared<FWebToUEDocument>();
	RuntimeDocument->SetSharedStyleTemplate(SharedStyleTemplate);

	TArray<TSharedPtr<FWebToUENode>> Nodes;
	if (!CompiledNodes.IsEmpty())
	{
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
	}
	Nodes.Reserve(CompiledNodes.Num());
	for (int32 CompiledNodeIndex = 0; CompiledNodeIndex < CompiledNodes.Num(); ++CompiledNodeIndex)
	{
		const FWebToUECompiledNode& Source = CompiledNodes[CompiledNodeIndex];
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		TSharedPtr<FWebToUENode> Node = MakeShared<FWebToUENode>();
		Node->TemplateNodeId = FWebToUETemplateNodeId::FromIndex(CompiledNodeIndex);
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

	RuntimeDocument->InitializeRuntimeData(OwnerId, Generation);
	for (const FWebToUECompiledBindingOp& CompiledOp : CompiledDocument.GetCompiledBindingOps())
	{
		if (CompiledOp.RootField.IsNone() || !Nodes.IsValidIndex(CompiledOp.TargetNodeIndex))
		{
			UE_LOG(LogWebToUERuntimeInstance, Error,
				TEXT("Failed to hydrate compiled binding op for field '%s' and node index %d."),
				*CompiledOp.RootField.ToString(), CompiledOp.TargetNodeIndex);
			Reset();
			return false;
		}
		FWebToUERuntimeBindingOp& RuntimeOp =
			BindingOpsByField.FindOrAdd(CompiledOp.RootField).AddDefaulted_GetRef();
		RuntimeOp.Kind = CompiledOp.Kind;
		RuntimeOp.Target = Nodes[CompiledOp.TargetNodeIndex]->InstanceHandle;
		RuntimeOp.bRichText = CompiledOp.bRichText;
	}
	FWebToUEStyleResolver::Resolve(*RuntimeDocument);
	return true;
}
