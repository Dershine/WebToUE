#include "WebToUEAnimation.h"

#include "Containers/Ticker.h"

namespace WebToUE::Animation::Private
{
	static bool IsFiniteColor(const FLinearColor& Color)
	{
		return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) &&
			FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
	}

	static bool IsFiniteTransform(const FWebToUEVisualTransformValue& Transform)
	{
		return FMath::IsFinite(Transform.M00) && FMath::IsFinite(Transform.M01) &&
			FMath::IsFinite(Transform.M10) && FMath::IsFinite(Transform.M11) &&
			FMath::IsFinite(Transform.TranslationPixels.X) &&
			FMath::IsFinite(Transform.TranslationPixels.Y) &&
			FMath::IsFinite(Transform.TranslationByWidth.X) &&
			FMath::IsFinite(Transform.TranslationByWidth.Y) &&
			FMath::IsFinite(Transform.TranslationByHeight.X) &&
			FMath::IsFinite(Transform.TranslationByHeight.Y);
	}
}

FWebToUEAnimationValue FWebToUEAnimationValue::MakeScalar(float InValue)
{
	FWebToUEAnimationValue Result;
	Result.Type = EWebToUEAnimationValueType::Scalar;
	Result.Scalar = InValue;
	return Result;
}

FWebToUEAnimationValue FWebToUEAnimationValue::MakeColor(const FLinearColor& InValue)
{
	FWebToUEAnimationValue Result;
	Result.Type = EWebToUEAnimationValueType::Color;
	Result.Color = InValue;
	return Result;
}

FWebToUEAnimationValue FWebToUEAnimationValue::MakeTransform(
	const FWebToUEVisualTransformValue& InValue)
{
	FWebToUEAnimationValue Result;
	Result.Type = EWebToUEAnimationValueType::Transform;
	Result.Transform = InValue;
	return Result;
}

FWebToUEAnimationValue FWebToUEAnimationValue::Interpolate(
	const FWebToUEAnimationValue& From,
	const FWebToUEAnimationValue& To,
	float Alpha)
{
	if (From.Type != To.Type)
	{
		return {};
	}
	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	switch (From.Type)
	{
	case EWebToUEAnimationValueType::Scalar:
		return MakeScalar(FMath::Lerp(From.Scalar, To.Scalar, Alpha));
	case EWebToUEAnimationValueType::Color:
		return MakeColor(FMath::Lerp(From.Color, To.Color, Alpha));
	case EWebToUEAnimationValueType::Transform:
	{
		FWebToUEVisualTransformValue Value;
		Value.M00 = FMath::Lerp(From.Transform.M00, To.Transform.M00, Alpha);
		Value.M01 = FMath::Lerp(From.Transform.M01, To.Transform.M01, Alpha);
		Value.M10 = FMath::Lerp(From.Transform.M10, To.Transform.M10, Alpha);
		Value.M11 = FMath::Lerp(From.Transform.M11, To.Transform.M11, Alpha);
		Value.TranslationPixels = FMath::Lerp(
			From.Transform.TranslationPixels, To.Transform.TranslationPixels, Alpha);
		Value.TranslationByWidth = FMath::Lerp(
			From.Transform.TranslationByWidth, To.Transform.TranslationByWidth, Alpha);
		Value.TranslationByHeight = FMath::Lerp(
			From.Transform.TranslationByHeight, To.Transform.TranslationByHeight, Alpha);
		return MakeTransform(Value);
	}
	default:
		return {};
	}
}

bool FWebToUEAnimationValue::IsFinite() const
{
	switch (Type)
	{
	case EWebToUEAnimationValueType::Scalar:
		return FMath::IsFinite(Scalar);
	case EWebToUEAnimationValueType::Color:
		return WebToUE::Animation::Private::IsFiniteColor(Color);
	case EWebToUEAnimationValueType::Transform:
		return WebToUE::Animation::Private::IsFiniteTransform(Transform);
	default:
		return false;
	}
}

bool FWebToUEAnimationValue::IsCompatibleWith(
	const FWebToUEPropertyAddress& Address) const
{
	if (!IsFinite() || !FWebToUEPropertyOwnershipPolicy::IsAnimationTarget(Address))
	{
		return false;
	}
	if (Address.Kind == EWebToUEPropertyTargetKind::VisualTransform)
	{
		return Type == EWebToUEAnimationValueType::Transform;
	}
	if (Address.Kind == EWebToUEPropertyTargetKind::MaterialParameter)
	{
		return (Address.MaterialParameterType == EWebToUEMaterialParameterType::Scalar &&
			Type == EWebToUEAnimationValueType::Scalar) ||
			(Address.MaterialParameterType == EWebToUEMaterialParameterType::Vector &&
			Type == EWebToUEAnimationValueType::Color);
	}
	if (Address.Kind == EWebToUEPropertyTargetKind::CssProperty)
	{
		return Address.CssProperty == EWebToUECssProperty::Opacity
			? Type == EWebToUEAnimationValueType::Scalar
			: Type == EWebToUEAnimationValueType::Color;
	}
	return false;
}

FWebToUEPropertyAddress FWebToUECompiledAnimationTarget::ToPropertyAddress() const
{
	switch (Kind)
	{
	case EWebToUECompiledAnimationTargetKind::Opacity:
		return FWebToUEPropertyAddress::Css(EWebToUECssProperty::Opacity);
	case EWebToUECompiledAnimationTargetKind::Color:
		return FWebToUEPropertyAddress::Css(EWebToUECssProperty::Color);
	case EWebToUECompiledAnimationTargetKind::BackgroundColor:
		return FWebToUEPropertyAddress::Css(EWebToUECssProperty::BackgroundColor);
	case EWebToUECompiledAnimationTargetKind::BorderColor:
		return FWebToUEPropertyAddress::Css(EWebToUECssProperty::BorderColor);
	case EWebToUECompiledAnimationTargetKind::VisualTransform:
		return FWebToUEPropertyAddress::VisualTransform();
	case EWebToUECompiledAnimationTargetKind::MaterialScalar:
		return FWebToUEPropertyAddress::Material(
			MaterialParameter, EWebToUEMaterialParameterType::Scalar);
	case EWebToUECompiledAnimationTargetKind::MaterialVector:
		return FWebToUEPropertyAddress::Material(
			MaterialParameter, EWebToUEMaterialParameterType::Vector);
	default:
		return {};
	}
}

EWebToUEAnimationValueType FWebToUECompiledAnimationTarget::GetValueType() const
{
	switch (Kind)
	{
	case EWebToUECompiledAnimationTargetKind::Opacity:
	case EWebToUECompiledAnimationTargetKind::MaterialScalar:
		return EWebToUEAnimationValueType::Scalar;
	case EWebToUECompiledAnimationTargetKind::Color:
	case EWebToUECompiledAnimationTargetKind::BackgroundColor:
	case EWebToUECompiledAnimationTargetKind::BorderColor:
	case EWebToUECompiledAnimationTargetKind::MaterialVector:
		return EWebToUEAnimationValueType::Color;
	case EWebToUECompiledAnimationTargetKind::VisualTransform:
		return EWebToUEAnimationValueType::Transform;
	default:
		return EWebToUEAnimationValueType::Invalid;
	}
}

bool FWebToUECompiledAnimationIR::Validate(
	int32 CompiledNodeCount,
	TArray<FWebToUEAnimationDiagnostic>& OutDiagnostics) const
{
	OutDiagnostics.Reset();
	if (Version != FWebToUEArtifactLayerVersion{ CurrentMajor, CurrentMinor })
	{
		OutDiagnostics.Add({ TEXT("WTUE-ANI-001"), TEXT("animation.version"),
			TEXT("Compiled Animation IR requires the supported 1.0 layer version.") });
	}
	TSet<FName> TrackIds;
	for (int32 Index = 0; Index < Tracks.Num(); ++Index)
	{
		const FWebToUECompiledAnimationTrack& Track = Tracks[Index];
		const FString Path = FString::Printf(TEXT("animation.tracks[%d]"), Index);
		const FWebToUEPropertyAddress Address = Track.Target.ToPropertyAddress();
		if (Track.TrackId.IsNone() || TrackIds.Contains(Track.TrackId))
		{
			OutDiagnostics.Add({ TEXT("WTUE-ANI-002"), Path + TEXT(".id"),
				TEXT("Animation Track IDs must be present and unique within the IR revision.") });
		}
		TrackIds.Add(Track.TrackId);
		if (Track.Target.TargetNodeIndex < 0 ||
			Track.Target.TargetNodeIndex >= CompiledNodeCount || !Address.IsValid())
		{
			OutDiagnostics.Add({ TEXT("WTUE-ANI-002"), Path + TEXT(".target"),
				TEXT("Animation target must resolve to a compiled node and canonical animatable address.") });
		}
		if (!Track.From.IsCompatibleWith(Address) || !Track.To.IsCompatibleWith(Address) ||
			Track.From.Type != Track.To.Type ||
			Track.From.Type != Track.Target.GetValueType())
		{
			OutDiagnostics.Add({ TEXT("WTUE-ANI-003"), Path + TEXT(".values"),
				TEXT("Animation endpoints must be finite, equally typed, and compatible with the target.") });
		}
		if (!FMath::IsFinite(Track.DurationSeconds) || Track.DurationSeconds <= 0.0)
		{
			OutDiagnostics.Add({ TEXT("WTUE-ANI-003"), Path + TEXT(".duration"),
				TEXT("Animation duration must be finite and greater than zero.") });
		}
	}
	OutDiagnostics.Sort([](const FWebToUEAnimationDiagnostic& A,
		const FWebToUEAnimationDiagnostic& B)
	{
		if (A.Code != B.Code) return A.Code < B.Code;
		if (A.Path != B.Path) return A.Path < B.Path;
		return A.Detail < B.Detail;
	});
	return OutDiagnostics.IsEmpty();
}

bool FWebToUEAnimationBudget::Validate(FString& OutError) const
{
	OutError.Reset();
	if (MaxActiveTracks <= 0 || MaxSamplesPerPump <= 0 || MaxTraceEntries <= 0)
	{
		OutError = TEXT("Every WebToUE animation budget must be greater than zero.");
		return false;
	}
	if (MaxSamplesPerPump < MaxActiveTracks)
	{
		OutError = TEXT("Animation sample budget must cover every allowed active Track.");
		return false;
	}
	return true;
}

TSharedRef<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe>
FWebToUEAnimationCoordinator::Create(
	FWebToUEAnimationOwnerHandle Owner,
	TSharedRef<IWebToUEClock> Clock,
	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Updates,
	const FWebToUEAnimationBudget& Budget)
{
	FString Error;
	checkf(Owner.IsValid(), TEXT("Animation coordinator requires a valid Session owner."));
	checkf(Budget.Validate(Error), TEXT("Invalid WebToUE animation budget: %s"), *Error);
	return MakeShareable(new FWebToUEAnimationCoordinator(
		Owner, Clock, Updates, Budget));
}

FWebToUEAnimationCoordinator::FWebToUEAnimationCoordinator(
	FWebToUEAnimationOwnerHandle InOwner,
	TSharedRef<IWebToUEClock> InClock,
	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> InUpdates,
	const FWebToUEAnimationBudget& InBudget)
	: Owner(InOwner)
	, Clock(InClock)
	, Updates(InUpdates)
	, Budget(InBudget)
{
}

FWebToUEAnimationCoordinator::~FWebToUEAnimationCoordinator()
{
	RemoveTicker();
}

FWebToUEAnimationStartOutcome FWebToUEAnimationCoordinator::StartTrack(
	const FWebToUEAnimationTrackRequest& Request,
	EWebToUEAnimationConflictPolicy ConflictPolicy)
{
	check(IsInGameThread());
	FWebToUEAnimationStartOutcome Immediate;
	if (!bActive || !Updates->IsActive())
	{
		Immediate.Diagnostic = TEXT("WTUE-ANI-006: animation coordinator is inactive.");
		return Immediate;
	}
	const TSharedPtr<IWebToUEAnimationTarget> Target = Request.TargetAdapter.Pin();
	if (!Request.Target.IsValid() || !Request.Address.IsValid() || !Target ||
		!Clock->SupportsDomain(Request.ClockDomain) ||
		!FMath::IsFinite(Request.DurationSeconds) || Request.DurationSeconds <= 0.0 ||
		Request.From.Type != Request.To.Type ||
		!Request.From.IsCompatibleWith(Request.Address) ||
		!Request.To.IsCompatibleWith(Request.Address))
	{
		Immediate.Result = EWebToUEAnimationStartResult::RejectedInvalidRequest;
		Immediate.Diagnostic = TEXT("WTUE-ANI-002: Track request is invalid or incompatibly typed.");
		return Immediate;
	}
	FString TargetError;
	if (!Target->ValidateAnimationTarget(
		Request.Target, Request.Address, Request.From.Type, TargetError))
	{
		Immediate.Result = EWebToUEAnimationStartResult::RejectedTarget;
		Immediate.Diagnostic = FString::Printf(TEXT("WTUE-ANI-004: %s"), *TargetError);
		return Immediate;
	}
	const FTargetKey Key{ Request.Target, Request.Address };
	if (!ActiveTracks.Contains(Key) && ActiveTracks.Num() >= Budget.MaxActiveTracks)
	{
		Immediate.Result = EWebToUEAnimationStartResult::RejectedBudget;
		Immediate.Diagnostic = TEXT("WTUE-ANI-005: active Track budget is exhausted.");
		return Immediate;
	}
	if (ActiveTracks.Contains(Key) &&
		ConflictPolicy == EWebToUEAnimationConflictPolicy::Reject)
	{
		Immediate.Result = EWebToUEAnimationStartResult::RejectedConflict;
		Immediate.Diagnostic = TEXT("WTUE-ANI-005: target address already has an active lease.");
		return Immediate;
	}

	const FWebToUEAnimationTrackHandle Handle{ Owner, NextTrackId++ };
	const TSharedRef<FWebToUEAnimationStartOutcome, ESPMode::ThreadSafe> Outcome =
		MakeShared<FWebToUEAnimationStartOutcome, ESPMode::ThreadSafe>();
	Outcome->Handle = Handle;
	const FWebToUEAnimationTrackRequest Captured = Request;
	const TWeakPtr<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe> WeakThis = AsShared();
	const EWebToUEUpdateSubmitResult SubmitResult = Updates->Submit(
		[WeakThis, Captured, ConflictPolicy, Handle, Outcome](
			FWebToUEUpdateTransaction& Transaction)
		{
			const TSharedPtr<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe> Coordinator =
				WeakThis.Pin();
			const TSharedPtr<IWebToUEAnimationTarget> TargetAdapter =
				Captured.TargetAdapter.Pin();
			if (!Coordinator || !Coordinator->bActive || !TargetAdapter)
			{
				Outcome->Result = EWebToUEAnimationStartResult::RejectedInactive;
				Outcome->Diagnostic = TEXT("WTUE-ANI-006: Track owner expired before evaluation.");
				Transaction.Reject(Outcome->Diagnostic);
				return;
			}
			const FTargetKey Key{ Captured.Target, Captured.Address };
			FWebToUEAnimationValue EffectiveFrom = Captured.From;
			EWebToUEAnimationTraceOutcome TraceOutcome =
				EWebToUEAnimationTraceOutcome::Started;
			EWebToUEAnimationStartResult StartResult =
				EWebToUEAnimationStartResult::Started;
			if (const FActiveTrack* Existing = Coordinator->ActiveTracks.Find(Key))
			{
				if (ConflictPolicy == EWebToUEAnimationConflictPolicy::Reject)
				{
					Outcome->Result = EWebToUEAnimationStartResult::RejectedConflict;
					Outcome->Diagnostic =
						TEXT("WTUE-ANI-005: target address acquired a lease before evaluation.");
					Transaction.Reject(Outcome->Diagnostic);
					return;
				}
				if (ConflictPolicy == EWebToUEAnimationConflictPolicy::Retarget)
				{
					float ExistingAlpha = 0.0f;
					EffectiveFrom = Sample(*Existing,
						Coordinator->Clock->GetTimeSeconds(Existing->ClockDomain),
						ExistingAlpha);
					TraceOutcome = EWebToUEAnimationTraceOutcome::Retargeted;
					StartResult = EWebToUEAnimationStartResult::Retargeted;
				}
				else
				{
					TraceOutcome = EWebToUEAnimationTraceOutcome::Replaced;
					StartResult = EWebToUEAnimationStartResult::Replaced;
				}
			}
			if (!Transaction.AddStateMutation(
				[WeakThis, Captured, Handle, EffectiveFrom, TraceOutcome,
					StartResult, Outcome]()
				{
					const TSharedPtr<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe> Commit =
						WeakThis.Pin();
					const TSharedPtr<IWebToUEAnimationTarget> CommitTarget =
						Captured.TargetAdapter.Pin();
					if (!Commit || !Commit->bActive || !CommitTarget) return;
					FActiveTrack Track;
					Track.Handle = Handle;
					Track.DebugName = Captured.DebugName;
					Track.Key = { Captured.Target, Captured.Address };
					Track.From = EffectiveFrom;
					Track.To = Captured.To;
					Track.StartTimeSeconds =
						Commit->Clock->GetTimeSeconds(Captured.ClockDomain);
					Track.DurationSeconds = Captured.DurationSeconds;
					Track.ClockDomain = Captured.ClockDomain;
					Track.TargetAdapter = Captured.TargetAdapter;
					if (const FActiveTrack* Superseded =
						Commit->ActiveTracks.Find(Track.Key))
					{
						const TSharedPtr<IWebToUEAnimationTarget> OldTarget =
							Superseded->TargetAdapter.Pin();
						if (OldTarget && OldTarget.Get() != CommitTarget.Get())
						{
							OldTarget->ReleaseAnimationOverlay(
								Track.Key.Target, Track.Key.Address);
						}
					}
					Commit->ActiveTracks.Add(Track.Key, Track);
					CommitTarget->ApplyAnimationOverlay(
						Track.Key.Target, Track.Key.Address, Track.From);
					Commit->AddTrace(
						Track, TraceOutcome, Track.StartTimeSeconds, 0.0f);
					Commit->EnsureTicker();
					Outcome->Result = StartResult;
				}))
			{
				Outcome->Result = EWebToUEAnimationStartResult::RejectedBudget;
				Outcome->Diagnostic =
					TEXT("WTUE-ANI-005: update mutation budget rejected Track start.");
			}
		});

	if (SubmitResult == EWebToUEUpdateSubmitResult::Executed)
	{
		return *Outcome;
	}
	if (SubmitResult == EWebToUEUpdateSubmitResult::RejectedInactive)
	{
		Immediate.Diagnostic =
			TEXT("WTUE-ANI-006: update coordinator rejected Track start.");
		return Immediate;
	}
	Outcome->Result = EWebToUEAnimationStartResult::Queued;
	return *Outcome;
}

EWebToUEAnimationCancelResult FWebToUEAnimationCoordinator::Cancel(
	FWebToUEAnimationTrackHandle Handle)
{
	check(IsInGameThread());
	if (!Handle.IsValid()) return EWebToUEAnimationCancelResult::RejectedInvalidHandle;
	if (!bActive || !Updates->IsActive())
		return EWebToUEAnimationCancelResult::RejectedInactive;
	if (Handle.Owner.SessionId != Owner.SessionId)
		return EWebToUEAnimationCancelResult::DroppedWrongSession;
	if (Handle.Owner.Generation != Owner.Generation)
		return EWebToUEAnimationCancelResult::DroppedStaleGeneration;
	TOptional<FTargetKey> FoundKey;
	for (const TPair<FTargetKey, FActiveTrack>& Pair : ActiveTracks)
	{
		if (Pair.Value.Handle == Handle)
		{
			FoundKey = Pair.Key;
			break;
		}
	}
	if (!FoundKey.IsSet())
		return EWebToUEAnimationCancelResult::DroppedSuperseded;
	const TWeakPtr<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe> WeakThis = AsShared();
	const EWebToUEUpdateSubmitResult SubmitResult = Updates->Submit(
		[WeakThis, Key = FoundKey.GetValue(), Handle](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation([WeakThis, Key, Handle]()
			{
				const TSharedPtr<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe> Commit =
					WeakThis.Pin();
				if (!Commit) return;
				FActiveTrack* Track = Commit->ActiveTracks.Find(Key);
				if (!Track || Track->Handle != Handle) return;
				if (const TSharedPtr<IWebToUEAnimationTarget> Target =
					Track->TargetAdapter.Pin())
				{
					Target->ReleaseAnimationOverlay(Key.Target, Key.Address);
				}
				Commit->AddTrace(*Track,
					EWebToUEAnimationTraceOutcome::CancelledExplicit,
					Commit->Clock->GetTimeSeconds(Track->ClockDomain), 0.0f);
				Commit->ActiveTracks.Remove(Key);
				if (Commit->ActiveTracks.IsEmpty()) Commit->RemoveTicker();
			});
		});
	if (SubmitResult == EWebToUEUpdateSubmitResult::Executed)
		return EWebToUEAnimationCancelResult::Cancelled;
	if (SubmitResult == EWebToUEUpdateSubmitResult::RejectedInactive)
		return EWebToUEAnimationCancelResult::RejectedInactive;
	return EWebToUEAnimationCancelResult::Queued;
}

int32 FWebToUEAnimationCoordinator::Pump()
{
	check(IsInGameThread());
	if (!bActive || ActiveTracks.IsEmpty()) return 0;
	struct FSampledTrack
	{
		FTargetKey Key;
		FWebToUEAnimationTrackHandle Handle;
		FWebToUEAnimationValue Value;
		double TimeSeconds = 0.0;
		float Alpha = 0.0f;
		bool bComplete = false;
	};
	TArray<FSampledTrack> Samples;
	Samples.Reserve(FMath::Min(ActiveTracks.Num(), Budget.MaxSamplesPerPump));
	for (const TPair<FTargetKey, FActiveTrack>& Pair : ActiveTracks)
	{
		if (Samples.Num() >= Budget.MaxSamplesPerPump) break;
		const FActiveTrack& Track = Pair.Value;
		const double Time = Clock->GetTimeSeconds(Track.ClockDomain);
		float Alpha = 0.0f;
		FSampledTrack& Entry = Samples.AddDefaulted_GetRef();
		Entry.Key = Pair.Key;
		Entry.Handle = Track.Handle;
		Entry.Value = Sample(Track, Time, Alpha);
		Entry.TimeSeconds = Time;
		Entry.Alpha = Alpha;
		Entry.bComplete = Alpha >= 1.0f;
	}
	if (Samples.IsEmpty()) return 0;
	const int32 SampleCount = Samples.Num();
	const TWeakPtr<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe> WeakThis = AsShared();
	Updates->Submit(
		[WeakThis, Samples = MoveTemp(Samples)](
			FWebToUEUpdateTransaction& Transaction) mutable
		{
			Transaction.AddStateMutation(
				[WeakThis, Samples = MoveTemp(Samples)]() mutable
				{
					const TSharedPtr<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe> Commit =
						WeakThis.Pin();
					if (!Commit || !Commit->bActive) return;
					for (const FSampledTrack& Sampled : Samples)
					{
						FActiveTrack* Track = Commit->ActiveTracks.Find(Sampled.Key);
						if (!Track || Track->Handle != Sampled.Handle) continue;
						const TSharedPtr<IWebToUEAnimationTarget> Target =
							Track->TargetAdapter.Pin();
						if (!Target)
						{
							Commit->AddTrace(*Track,
								EWebToUEAnimationTraceOutcome::DroppedTarget,
								Sampled.TimeSeconds, Sampled.Alpha,
								TEXT("Animation target expired."));
							Commit->ActiveTracks.Remove(Sampled.Key);
							continue;
						}
						Target->ApplyAnimationOverlay(
							Sampled.Key.Target, Sampled.Key.Address, Sampled.Value);
						Commit->AddTrace(*Track,
							Sampled.bComplete
								? EWebToUEAnimationTraceOutcome::Completed
								: EWebToUEAnimationTraceOutcome::Sampled,
							Sampled.TimeSeconds, Sampled.Alpha);
						if (Sampled.bComplete)
						{
							Target->ReleaseAnimationOverlay(
								Sampled.Key.Target, Sampled.Key.Address);
							Commit->ActiveTracks.Remove(Sampled.Key);
						}
					}
					if (Commit->ActiveTracks.IsEmpty()) Commit->RemoveTicker();
				});
		});
	return SampleCount;
}

void FWebToUEAnimationCoordinator::AdvanceGeneration(
	FWebToUEAnimationOwnerHandle NewOwner)
{
	check(IsInGameThread());
	if (!bActive || NewOwner.SessionId != Owner.SessionId ||
		NewOwner.Generation == Owner.Generation || !NewOwner.IsValid())
	{
		return;
	}
	CancelAll(EWebToUEAnimationTraceOutcome::CancelledGeneration);
	Owner = NewOwner;
}

void FWebToUEAnimationCoordinator::Shutdown()
{
	check(IsInGameThread());
	if (!bActive) return;
	CancelAll(EWebToUEAnimationTraceOutcome::CancelledSession);
	bActive = false;
	RemoveTicker();
}

bool FWebToUEAnimationCoordinator::IsTickerRegistered() const
{
	return TickerHandle.IsValid();
}

bool FWebToUEAnimationCoordinator::Tick(float DeltaSeconds)
{
	++TickerInvocationCount;
	Pump();
	return bActive && !ActiveTracks.IsEmpty();
}

void FWebToUEAnimationCoordinator::EnsureTicker()
{
	if (!TickerHandle.IsValid() && bActive && !ActiveTracks.IsEmpty())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(AsShared(), &FWebToUEAnimationCoordinator::Tick));
	}
}

void FWebToUEAnimationCoordinator::RemoveTicker()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

void FWebToUEAnimationCoordinator::CancelAll(
	EWebToUEAnimationTraceOutcome Outcome)
{
	for (TPair<FTargetKey, FActiveTrack>& Pair : ActiveTracks)
	{
		FActiveTrack& Track = Pair.Value;
		if (const TSharedPtr<IWebToUEAnimationTarget> Target =
			Track.TargetAdapter.Pin())
		{
			Target->ReleaseAnimationOverlay(Pair.Key.Target, Pair.Key.Address);
		}
		AddTrace(Track, Outcome,
			Clock->GetTimeSeconds(Track.ClockDomain), 0.0f);
	}
	ActiveTracks.Reset();
	RemoveTicker();
}

void FWebToUEAnimationCoordinator::AddTrace(
	const FActiveTrack& Track,
	EWebToUEAnimationTraceOutcome Outcome,
	double TimeSeconds,
	float Alpha,
	FString Diagnostic)
{
	if (Trace.Num() >= Budget.MaxTraceEntries)
	{
		Trace.RemoveAt(
			0, Trace.Num() - Budget.MaxTraceEntries + 1, EAllowShrinking::No);
	}
	FWebToUEAnimationTrace& Entry = Trace.AddDefaulted_GetRef();
	Entry.Handle = Track.Handle;
	Entry.DebugName = Track.DebugName;
	Entry.Address = Track.Key.Address.ToString();
	Entry.Outcome = Outcome;
	Entry.TimeSeconds = TimeSeconds;
	Entry.Alpha = Alpha;
	Entry.Diagnostic = MoveTemp(Diagnostic);
}

FWebToUEAnimationValue FWebToUEAnimationCoordinator::Sample(
	const FActiveTrack& Track, double TimeSeconds, float& OutAlpha)
{
	OutAlpha = static_cast<float>(FMath::Clamp(
		(TimeSeconds - Track.StartTimeSeconds) / Track.DurationSeconds,
		0.0, 1.0));
	return FWebToUEAnimationValue::Interpolate(
		Track.From, Track.To, OutAlpha);
}
