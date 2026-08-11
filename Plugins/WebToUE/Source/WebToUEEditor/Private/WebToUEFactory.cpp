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

static void SerializeCompiledDocument(const FWebToUEDocument& Source, UWebToUEDocument& Target)
{
	TMap<FString, FString> PreviousAutoKeys;
	for (const FWebToUECompiledNode& Existing : Target.CompiledNodes)
	{
		if (Existing.bAutoLocalizationKey && !Existing.TextIdentity.IsEmpty() && !Existing.LocalizationKey.IsEmpty())
		{
			PreviousAutoKeys.Add(Existing.TextIdentity, Existing.LocalizationKey);
		}
	}
	if (Target.LocalizationNamespace.IsEmpty())
	{
		Target.LocalizationNamespace = TEXT("WebToUE_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}

	Target.CompiledNodes.Reset();
	Target.CompiledRules.Reset();
	Target.RootNodeIndex = INDEX_NONE;
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
				if (Serialized.LocalizationNamespace.IsEmpty()) Serialized.LocalizationNamespace = Target.LocalizationNamespace;
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
		const int32 NodeIndex = Target.CompiledNodes.Add(MoveTemp(Serialized));
		TMap<FString, int32> ChildOrdinals;
		for (const TSharedPtr<FWebToUENode>& Child : Node->Children)
		{
			const FString Kind = Child->Type == EWebToUENodeType::Text ? TEXT("text") : Child->Tag;
			const int32 ChildOrdinal = ChildOrdinals.FindOrAdd(Kind)++;
			AddNode(Child, NodeIndex, Target.CompiledNodes[NodeIndex].TextIdentity, ChildOrdinal);
		}
		return NodeIndex;
	};
	if (Source.Root) Target.RootNodeIndex = AddNode(Source.Root, INDEX_NONE, TEXT("document"), 0);

	for (const FWebToUEStyleRule& Rule : Source.Rules)
	{
		FWebToUECompiledRule& SerializedRule = Target.CompiledRules.AddDefaulted_GetRef();
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
		for (const TPair<FString, FString>& Declaration : Rule.Declarations)
		{
			FWebToUECompiledDeclaration& SerializedDeclaration = SerializedRule.Declarations.AddDefaulted_GetRef();
			SerializedDeclaration.Name = Declaration.Key;
			SerializedDeclaration.Value = Declaration.Value;
		}
	}
}

bool UWebToUEFactory::ImportIntoDocument(UWebToUEDocument& Document, const FString& Filename, bool bPreserveLastGood)
{
	FString Html;
	if (!FFileHelper::LoadFileToString(Html, *Filename))
	{
		UE_LOG(LogWebToUEImport, Error, TEXT("Could not read WebToUE document '%s'."), *Filename);
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
	if (!bHasErrors || !bPreserveLastGood || Document.CompiledNodes.IsEmpty())
	{
		Document.CompiledHtml = bHasErrors ? FString() : Html;
		Document.CompiledCss = bHasErrors ? FString() : CombinedCss;
		if (bHasErrors)
		{
			Document.CompiledNodes.Reset();
			Document.CompiledRules.Reset();
			Document.RootNodeIndex = INDEX_NONE;
		}
		else
		{
			SerializeCompiledDocument(*Compiled, Document);
			Document.MarkRecompiled();
		}
		Document.ReferencedTextures.Reset();
		Document.ReferencedStringTables.Reset();
		if (!bHasErrors)
		{
			Compiled->ForEachNode([&Document](FWebToUENode& Node)
			{
				if (Node.Tag == TEXT("img"))
				{
					const FSoftObjectPath Path(Node.GetAttribute(TEXT("src")));
					if (Path.IsValid()) Document.ReferencedTextures.AddUnique(TSoftObjectPtr<UTexture2D>(Path));
				}
				const FString StringTable = Node.GetAttribute(TEXT("data-ue-string-table"));
				if (!StringTable.IsEmpty())
				{
					const FSoftObjectPath Path(StringTable);
					if (Path.IsValid()) Document.ReferencedStringTables.AddUnique(TSoftObjectPtr<UStringTable>(Path));
				}
			});
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
	if (!IFileManager::Get().FileExists(*Filename)) return EReimportResult::Failed;
	Document->PreEditChange(nullptr);
	const bool bSucceeded = ImportIntoDocument(*Document, Filename, true);
	Document->PostEditChange();
	return bSucceeded ? EReimportResult::Succeeded : EReimportResult::Failed;
#else
	return EReimportResult::Failed;
#endif
}
