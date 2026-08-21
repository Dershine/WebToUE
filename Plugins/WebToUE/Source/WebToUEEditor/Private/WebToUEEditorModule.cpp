#include "Modules/ModuleManager.h"

#include "WebToUEDocument.h"
#include "WebToUEFeedbackProfile.h"
#include "WebToUEFactory.h"

#include "DirectoryWatcherModule.h"
#include "EditorReimportHandler.h"
#include "IDirectoryWatcher.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogWebToUEEditor, Log, All);

class FWebToUEEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UWebToUEDocument::CookFreshnessValidator().BindStatic(
			&UWebToUEFactory::ValidateCookFreshness);
		UWebToUEFeedbackProfile::CookFreshnessValidator().BindStatic(
			&UWebToUEFeedbackProfile::ValidateCurrentCookFreshness);
		RecompileRequestedHandle = UWebToUEDocument::OnDocumentNeedsRecompile().AddRaw(
			this, &FWebToUEEditorModule::QueueVersionRecompile);
		for (TObjectIterator<UWebToUEDocument> It; It; ++It)
		{
			QueueVersionRecompile(*It);
		}
		if (!FPaths::ProjectDir().IsEmpty())
		{
			FDirectoryWatcherModule& Module = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
			DirectoryWatcher = Module.Get();
			if (DirectoryWatcher)
			{
				DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
					FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()),
					IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FWebToUEEditorModule::OnDirectoryChanged),
					WatchHandle);
			}
		}
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FWebToUEEditorModule::Tick), 0.1f);
	}

	virtual void ShutdownModule() override
	{
		UWebToUEDocument::CookFreshnessValidator().Unbind();
		UWebToUEFeedbackProfile::CookFreshnessValidator().Unbind();
		if (TickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		if (RecompileRequestedHandle.IsValid())
		{
			UWebToUEDocument::OnDocumentNeedsRecompile().Remove(RecompileRequestedHandle);
		}
		if (DirectoryWatcher && WatchHandle.IsValid())
		{
			DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(
				FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), WatchHandle);
		}
		PendingFiles.Reset();
		PendingVersionReimports.Reset();
		DirectoryWatcher = nullptr;
	}

private:
	IDirectoryWatcher* DirectoryWatcher = nullptr;
	FDelegateHandle WatchHandle;
	FDelegateHandle RecompileRequestedHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	TMap<FString, double> PendingFiles;
	TSet<TWeakObjectPtr<UWebToUEDocument>> PendingVersionReimports;

	static bool IsWatchedSourceExtension(const FString& Extension)
	{
		static const TSet<FString> Extensions = {
			TEXT("html"), TEXT("css"), TEXT("bmp"), TEXT("png"),
			TEXT("jpg"), TEXT("jpeg"), TEXT("tga"), TEXT("psd"),
			TEXT("dds"), TEXT("exr"), TEXT("hdr")
		};
		return Extensions.Contains(Extension);
	}

	static bool IsPersistentDocument(const UWebToUEDocument* Document)
	{
		return Document && !Document->HasAnyFlags(RF_ClassDefaultObject | RF_Transient) &&
			Document->GetOutermost() != GetTransientPackage();
	}

	void QueueVersionRecompile(UWebToUEDocument* Document)
	{
		if (IsPersistentDocument(Document) && Document->NeedsRecompile())
		{
			PendingVersionReimports.Add(Document);
		}
	}

	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
	{
		const double ReadyAt = FPlatformTime::Seconds() + 0.2;
		for (const FFileChangeData& Change : Changes)
		{
			const FString Extension = FPaths::GetExtension(Change.Filename).ToLower();
			if (IsWatchedSourceExtension(Extension))
			{
				FString Normalized = FPaths::ConvertRelativePathToFull(Change.Filename);
				FPaths::NormalizeFilename(Normalized);
				PendingFiles.Add(Normalized, ReadyAt);
			}
		}
	}

	bool Tick(float DeltaTime)
	{
		if (IsEngineExitRequested()) return true;
		const double Now = FPlatformTime::Seconds();
		TArray<FString> Ready;
		for (const TPair<FString, double>& Pair : PendingFiles)
		{
			if (Pair.Value <= Now) Ready.Add(Pair.Key);
		}
		for (const FString& File : Ready) PendingFiles.Remove(File);

		TArray<TWeakObjectPtr<UWebToUEDocument>> VersionReimports = PendingVersionReimports.Array();
		PendingVersionReimports.Reset();
		for (const TWeakObjectPtr<UWebToUEDocument>& WeakDocument : VersionReimports)
		{
			if (UWebToUEDocument* Document = WeakDocument.Get(); Document && Document->NeedsRecompile())
			{
				UE_LOG(LogWebToUEEditor, Display,
					TEXT("Reimporting legacy WebToUE document '%s' for the current compiled asset version."),
					*Document->GetPathName());
				FReimportManager::Instance()->Reimport(Document, false, false);
			}
		}

		if (Ready.IsEmpty()) return true;

		for (TObjectIterator<UWebToUEDocument> It; It; ++It)
		{
			UWebToUEDocument* Document = *It;
			if (!IsPersistentDocument(Document)) continue;
#if WITH_EDITORONLY_DATA
			const bool bAffected = Document->DependencyFiles.ContainsByPredicate([&Ready](FString Dependency)
			{
				Dependency = FPaths::ConvertRelativePathToFull(Dependency);
				FPaths::NormalizeFilename(Dependency);
				return Ready.Contains(Dependency);
			});
			if (bAffected) FReimportManager::Instance()->Reimport(Document, false, false);
#endif
		}
		return true;
	}
};

IMPLEMENT_MODULE(FWebToUEEditorModule, WebToUEEditor)
