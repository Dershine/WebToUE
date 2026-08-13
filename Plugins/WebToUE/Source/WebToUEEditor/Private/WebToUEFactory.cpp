#include "WebToUEFactory.h"

#include "WebToUECompiler.h"
#include "WebToUEDocument.h"

#include "EditorFramework/AssetImportData.h"
#include "Internationalization/StringTable.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogWebToUEImport, Log, All);

UWebToUEFactory::UWebToUEFactory()
{
	SupportedClass = UWebToUEDocument::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bText = true;
	Formats.Add(TEXT("html;WebToUE HTML Document"));
}

bool UWebToUEFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("html"), ESearchCase::IgnoreCase);
}

static FWebToUEAssetDiagnostic ConvertDiagnostic(const FWebToUEDiagnostic& Source)
{
	FWebToUEAssetDiagnostic Result;
	Result.Severity = Source.Severity == EWebToUEDiagnosticSeverity::Error ? EWebToUEAssetDiagnosticSeverity::Error :
		Source.Severity == EWebToUEDiagnosticSeverity::Warning ? EWebToUEAssetDiagnosticSeverity::Warning : EWebToUEAssetDiagnosticSeverity::Info;
	Result.File = Source.File;
	Result.Line = Source.Line;
	Result.Column = Source.Column;
	Result.Message = Source.Message;
	return Result;
}

static FWebToUECompiledDocumentData BuildCompiledDocument(const FWebToUEDocument& Source,
	const UWebToUEDocument& Target)
{
	FWebToUECompiledDocumentData Result;
	TMap<FString, FString> PreviousAutoKeys;
	for (const FWebToUECompiledNode& Existing : Target.GetCompiledNodes())
	{
		if (Existing.bAutoLocalizationKey && !Existing.TextIdentity.IsEmpty() && !Existing.LocalizationKey.IsEmpty())
		{
			PreviousAutoKeys.Add(Existing.TextIdentity, Existing.LocalizationKey);
		}
	}
	Result.LocalizationNamespace = Target.GetLocalizationNamespace();
	if (Result.LocalizationNamespace.IsEmpty())
	{
		Result.LocalizationNamespace = TEXT("WebToUE_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}

	TFunction<int32(const TSharedPtr<FWebToUENode>&, int32, const FString&, int32)> AddNode =
		[&](const TSharedPtr<FWebToUENode>& Node, int32 ParentIndex, const FString& ParentIdentity, int32 SiblingOrdinal)
	{
		FWebToUECompiledNode Serialized;
		Serialized.Type = static_cast<uint8>(Node->Type);
		Serialized.Tag = Node->Tag;
		Serialized.Text = Node->Text;
		Serialized.bRichText = Node->bRichText;
		Serialized.ParentIndex = ParentIndex;
		const FString ElementId = Node->Type == EWebToUENodeType::Element ? Node->GetAttribute(TEXT("id")) : FString();
		Serialized.TextIdentity = !ElementId.IsEmpty()
			? TEXT("#") + ElementId
			: FString::Printf(TEXT("%s/%s[%d]"), *ParentIdentity,
				Node->Type == EWebToUENodeType::Text ? TEXT("text") : *Node->Tag, SiblingOrdinal);
		for (const TPair<FString, FString>& Attribute : Node->Attributes)
		{
			FWebToUECompiledAttribute& OutAttribute = Serialized.Attributes.AddDefaulted_GetRef();
			OutAttribute.Name = Attribute.Key;
			OutAttribute.Value = Attribute.Value;
		}
		for (const FWebToUEStyleDeclaration& Declaration : Node->InlineStyleDeclarations)
		{
			FWebToUECompiledDeclaration& OutDeclaration = Serialized.InlineStyleDeclarations.AddDefaulted_GetRef();
			OutDeclaration.Property = Declaration.Property;
			OutDeclaration.TypedValue = Declaration.TypedValue;
		}
		if (Node->Type == EWebToUENodeType::Text)
		{
			const FWebToUENode* Owner = Node->Parent;
			const FString TableId = Owner ? Owner->GetAttribute(TEXT("data-ue-string-table")) : FString();
			const FString TableKey = Owner ? Owner->GetAttribute(TEXT("data-ue-string-key")) : FString();
			if (!TableId.IsEmpty() && !TableKey.IsEmpty())
			{
				Serialized.StringTableId = FName(*TableId);
				Serialized.StringTableKey = TableKey;
				Serialized.LocalizedText = FText::FromStringTable(Serialized.StringTableId, TableKey);
			}
			else
			{
				Serialized.LocalizationNamespace = Owner ? Owner->GetAttribute(TEXT("data-ue-loc-namespace")) : FString();
				if (Serialized.LocalizationNamespace.IsEmpty()) Serialized.LocalizationNamespace = Result.LocalizationNamespace;
				Serialized.LocalizationKey = Owner ? Owner->GetAttribute(TEXT("data-ue-loc-key")) : FString();
				Serialized.bAutoLocalizationKey = Serialized.LocalizationKey.IsEmpty();
				if (Serialized.bAutoLocalizationKey)
				{
					if (const FString* ExistingKey = PreviousAutoKeys.Find(Serialized.TextIdentity))
					{
						Serialized.LocalizationKey = *ExistingKey;
					}
					else
					{
						Serialized.LocalizationKey = FGuid::NewGuid().ToString(EGuidFormats::Digits);
					}
				}
				Serialized.LocalizedText = FText::ChangeKey(Serialized.LocalizationNamespace,
					Serialized.LocalizationKey, FText::FromString(Node->Text));
			}
		}
		const int32 NodeIndex = Result.Nodes.Add(MoveTemp(Serialized));
		const auto AddBindingOp = [&](const TCHAR* AttributeName, EWebToUEBindingKind Kind)
		{
			const FString RootField = Node->GetAttribute(AttributeName);
			if (RootField.IsEmpty()) return;
			FWebToUECompiledBindingOp& BindingOp = Result.BindingOps.AddDefaulted_GetRef();
			BindingOp.RootField = FName(*RootField);
			BindingOp.Kind = Kind;
			BindingOp.TargetNodeIndex = NodeIndex;
			BindingOp.bRichText = Kind == EWebToUEBindingKind::Text &&
				Node->GetAttribute(TEXT("data-ue-rich-text")).Equals(
					TEXT("true"), ESearchCase::IgnoreCase);
		};
		AddBindingOp(TEXT("data-ue-bind-text"), EWebToUEBindingKind::Text);
		AddBindingOp(TEXT("data-ue-bind-visible"), EWebToUEBindingKind::Visible);
		AddBindingOp(TEXT("data-ue-bind-enabled"), EWebToUEBindingKind::Enabled);
		TMap<FString, int32> ChildOrdinals;
		for (const TSharedPtr<FWebToUENode>& Child : Node->Children)
		{
			const FString Kind = Child->Type == EWebToUENodeType::Text ? TEXT("text") : Child->Tag;
			const int32 ChildOrdinal = ChildOrdinals.FindOrAdd(Kind)++;
			AddNode(Child, NodeIndex, Result.Nodes[NodeIndex].TextIdentity, ChildOrdinal);
		}
		return NodeIndex;
	};
	if (Source.Root) Result.RootNodeIndex = AddNode(Source.Root, INDEX_NONE, TEXT("document"), 0);

	for (const FWebToUEStyleRule& Rule : Source.Rules)
	{
		FWebToUECompiledRule& SerializedRule = Result.Rules.AddDefaulted_GetRef();
		SerializedRule.Specificity = Rule.Specificity;
		SerializedRule.SourceOrder = Rule.SourceOrder;
		for (const FWebToUESelectorSegment& Segment : Rule.Selector)
		{
			FWebToUECompiledSelectorSegment& SerializedSegment = SerializedRule.Selector.AddDefaulted_GetRef();
			SerializedSegment.Type = Segment.Type;
			SerializedSegment.Id = Segment.Id;
			SerializedSegment.Classes = Segment.Classes;
			SerializedSegment.RequiredState = static_cast<uint8>(Segment.RequiredState);
			SerializedSegment.RelationToPrevious = static_cast<uint8>(Segment.RelationToPrevious);
		}
		for (const FWebToUEStyleDeclaration& Declaration : Rule.Declarations)
		{
			FWebToUECompiledDeclaration& SerializedDeclaration = SerializedRule.Declarations.AddDefaulted_GetRef();
			SerializedDeclaration.Property = Declaration.Property;
			SerializedDeclaration.TypedValue = Declaration.TypedValue;
		}
	}

	Source.ForEachNode([&Result](FWebToUENode& Node)
	{
		if (Node.Tag == TEXT("img"))
		{
			const FSoftObjectPath Path(Node.GetAttribute(TEXT("src")));
			if (Path.IsValid()) Result.ReferencedTextures.AddUnique(TSoftObjectPtr<UTexture2D>(Path));
		}
		const FString StringTable = Node.GetAttribute(TEXT("data-ue-string-table"));
		if (!StringTable.IsEmpty())
		{
			const FSoftObjectPath Path(StringTable);
			if (Path.IsValid()) Result.ReferencedStringTables.AddUnique(TSoftObjectPtr<UStringTable>(Path));
		}
	});
	return Result;
}

bool UWebToUEFactory::ImportIntoDocument(UWebToUEDocument& Document, const FString& Filename, bool bPreserveLastGood)
{
	FString Html;
	if (!FFileHelper::LoadFileToString(Html, *Filename))
	{
		const FString Message = FString::Printf(TEXT("Could not read WebToUE document '%s'."), *Filename);
		UE_LOG(LogWebToUEImport, Error, TEXT("%s"), *Message);
		Document.Diagnostics.Reset();
		FWebToUEAssetDiagnostic& Diagnostic = Document.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Severity = EWebToUEAssetDiagnosticSeverity::Error;
		Diagnostic.File = Filename;
		Diagnostic.Line = 1;
		Diagnostic.Column = 1;
		Diagnostic.Message = Message;
		if (!bPreserveLastGood || Document.GetCompiledNodes().IsEmpty())
		{
			Document.CompiledHtml.Reset();
			Document.CompiledCss.Reset();
			FWebToUECompiledDocumentData EmptyDocument;
			EmptyDocument.LocalizationNamespace = Document.GetLocalizationNamespace();
			Document.CommitCompiledDocument(MoveTemp(EmptyDocument));
		}
		Document.MarkPackageDirty();
		Document.NotifyDocumentChanged();
		return false;
	}

	const TSharedRef<FWebToUEDocument> LinkScan = FWebToUECompiler::Compile(Html, FString(), Filename);
	FString CombinedCss;
	TArray<FWebToUEStyleSheetSource> ExternalStyleSheets;
	TArray<FString> Dependencies;
	Dependencies.Add(FPaths::ConvertRelativePathToFull(Filename));
	TArray<FWebToUEDiagnostic> LinkErrors;
	for (const FString& RelativeHref : LinkScan->LinkedStylesheets)
	{
		FString CssPath = RelativeHref;
		if (FPaths::IsRelative(CssPath)) CssPath = FPaths::Combine(FPaths::GetPath(Filename), CssPath);
		CssPath = FPaths::ConvertRelativePathToFull(CssPath);
		FPaths::NormalizeFilename(CssPath);
		FString Css;
		if (FFileHelper::LoadFileToString(Css, *CssPath))
		{
			CombinedCss += FString::Printf(TEXT("\n/* %s */\n%s\n"), *CssPath, *Css);
			ExternalStyleSheets.Add({ MoveTemp(Css), CssPath, 1, 1 });
			Dependencies.AddUnique(CssPath);
		}
		else
		{
			LinkErrors.Add({ EWebToUEDiagnosticSeverity::Error, CssPath, 1, 1,
				FString::Printf(TEXT("Could not read linked stylesheet '%s'."), *RelativeHref) });
		}
	}

	const TSharedRef<FWebToUEDocument> Compiled = FWebToUECompiler::Compile(Html, ExternalStyleSheets, Filename);
	Compiled->Diagnostics.Append(LinkErrors);
	Document.Diagnostics.Reset();
	for (const FWebToUEDiagnostic& Diagnostic : Compiled->Diagnostics)
	{
		Document.Diagnostics.Add(ConvertDiagnostic(Diagnostic));
		if (Diagnostic.Severity == EWebToUEDiagnosticSeverity::Error)
		{
			UE_LOG(LogWebToUEImport, Error, TEXT("%s(%d,%d): %s"), *Diagnostic.File, Diagnostic.Line, Diagnostic.Column, *Diagnostic.Message);
		}
		else if (Diagnostic.Severity == EWebToUEDiagnosticSeverity::Warning)
		{
			UE_LOG(LogWebToUEImport, Warning, TEXT("%s(%d,%d): %s"), *Diagnostic.File, Diagnostic.Line, Diagnostic.Column, *Diagnostic.Message);
		}
	}

	const bool bHasErrors = Compiled->HasErrors();
	if (!bHasErrors || !bPreserveLastGood || Document.GetCompiledNodes().IsEmpty())
	{
		Document.CompiledHtml = bHasErrors ? FString() : Html;
		Document.CompiledCss = bHasErrors ? FString() : CombinedCss;
		if (bHasErrors)
		{
			FWebToUECompiledDocumentData EmptyDocument;
			EmptyDocument.LocalizationNamespace = Document.GetLocalizationNamespace();
			Document.CommitCompiledDocument(MoveTemp(EmptyDocument));
		}
		else
		{
			Document.CommitCompiledDocument(BuildCompiledDocument(*Compiled, Document));
			Document.MarkRecompiled();
		}
	}

#if WITH_EDITORONLY_DATA
	if (!Document.AssetImportData) Document.AssetImportData = NewObject<UAssetImportData>(&Document, TEXT("AssetImportData"));
	Document.AssetImportData->Update(Filename);
	Document.DependencyFiles = MoveTemp(Dependencies);
#endif
	Document.MarkPackageDirty();
	Document.NotifyDocumentChanged();
	return !bHasErrors;
}

UObject* UWebToUEFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(InParent, InClass, InName, Flags);
	ImportIntoDocument(*Document, Filename, false);
	return Document;
}

bool UWebToUEFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	if (const UWebToUEDocument* Document = Cast<UWebToUEDocument>(Obj))
	{
#if WITH_EDITORONLY_DATA
		if (Document->AssetImportData)
		{
			Document->AssetImportData->ExtractFilenames(OutFilenames);
			return OutFilenames.Num() > 0;
		}
#endif
	}
	return false;
}

void UWebToUEFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
#if WITH_EDITORONLY_DATA
	if (UWebToUEDocument* Document = Cast<UWebToUEDocument>(Obj); Document && Document->AssetImportData && NewReimportPaths.Num() == 1)
	{
		Document->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
#endif
}

EReimportResult::Type UWebToUEFactory::Reimport(UObject* Obj)
{
	UWebToUEDocument* Document = Cast<UWebToUEDocument>(Obj);
	if (!Document) return EReimportResult::Failed;
#if WITH_EDITORONLY_DATA
	if (!Document->AssetImportData) return EReimportResult::Failed;
	const FString Filename = Document->AssetImportData->GetFirstFilename();
	Document->PreEditChange(nullptr);
	const bool bSucceeded = ImportIntoDocument(*Document, Filename, true);
	Document->PostEditChange();
	return bSucceeded ? EReimportResult::Succeeded : EReimportResult::Failed;
#else
	return EReimportResult::Failed;
#endif
}
