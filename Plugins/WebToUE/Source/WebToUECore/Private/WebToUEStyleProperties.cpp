#include "WebToUEStyleProperties.h"

#include "String/LexFromString.h"

#include <initializer_list>

namespace WebToUE::Private
{
	namespace
	{
		constexpr EWebToUEStyleImpact StyleImpact = EWebToUEStyleImpact::Style;
		constexpr EWebToUEStyleImpact Paint = StyleImpact | EWebToUEStyleImpact::Paint;
		constexpr EWebToUEStyleImpact PaintHitTest = Paint | EWebToUEStyleImpact::HitTest;
		constexpr EWebToUEStyleImpact Layout = PaintHitTest | EWebToUEStyleImpact::Layout;
		constexpr EWebToUEStyleImpact Measure = Layout | EWebToUEStyleImpact::Measure;
		constexpr EWebToUEStyleImpact MeasureResource = Measure | EWebToUEStyleImpact::Resource;

		// Property IDs are serialized and append-only, so this table deliberately follows their numeric order.
		constexpr FWebToUECssPropertyMetadata PropertyMetadata[] = {
			{ EWebToUECssProperty::Invalid, TEXT("invalid"), false, EWebToUEStyleImpact::None },
			{ EWebToUECssProperty::Display, TEXT("display"), false, Layout },
			{ EWebToUECssProperty::Position, TEXT("position"), false, Layout },
			{ EWebToUECssProperty::Visibility, TEXT("visibility"), false, PaintHitTest },
			{ EWebToUECssProperty::Overflow, TEXT("overflow"), false, Layout },
			{ EWebToUECssProperty::Width, TEXT("width"), false, Layout },
			{ EWebToUECssProperty::Height, TEXT("height"), false, Layout },
			{ EWebToUECssProperty::MinWidth, TEXT("min-width"), false, Layout },
			{ EWebToUECssProperty::MinHeight, TEXT("min-height"), false, Layout },
			{ EWebToUECssProperty::MaxWidth, TEXT("max-width"), false, Layout },
			{ EWebToUECssProperty::MaxHeight, TEXT("max-height"), false, Layout },
			{ EWebToUECssProperty::Left, TEXT("left"), false, Layout },
			{ EWebToUECssProperty::Top, TEXT("top"), false, Layout },
			{ EWebToUECssProperty::Right, TEXT("right"), false, Layout },
			{ EWebToUECssProperty::Bottom, TEXT("bottom"), false, Layout },
			{ EWebToUECssProperty::Margin, TEXT("margin"), false, Layout },
			{ EWebToUECssProperty::MarginLeft, TEXT("margin-left"), false, Layout },
			{ EWebToUECssProperty::MarginTop, TEXT("margin-top"), false, Layout },
			{ EWebToUECssProperty::MarginRight, TEXT("margin-right"), false, Layout },
			{ EWebToUECssProperty::MarginBottom, TEXT("margin-bottom"), false, Layout },
			{ EWebToUECssProperty::Padding, TEXT("padding"), false, Layout },
			{ EWebToUECssProperty::PaddingLeft, TEXT("padding-left"), false, Layout },
			{ EWebToUECssProperty::PaddingTop, TEXT("padding-top"), false, Layout },
			{ EWebToUECssProperty::PaddingRight, TEXT("padding-right"), false, Layout },
			{ EWebToUECssProperty::PaddingBottom, TEXT("padding-bottom"), false, Layout },
			{ EWebToUECssProperty::Gap, TEXT("gap"), false, Layout },
			{ EWebToUECssProperty::RowGap, TEXT("row-gap"), false, Layout },
			{ EWebToUECssProperty::ColumnGap, TEXT("column-gap"), false, Layout },
			{ EWebToUECssProperty::Flex, TEXT("flex"), false, Layout },
			{ EWebToUECssProperty::FlexDirection, TEXT("flex-direction"), false, Layout },
			{ EWebToUECssProperty::FlexWrap, TEXT("flex-wrap"), false, Layout },
			{ EWebToUECssProperty::FlexGrow, TEXT("flex-grow"), false, Layout },
			{ EWebToUECssProperty::FlexShrink, TEXT("flex-shrink"), false, Layout },
			{ EWebToUECssProperty::FlexBasis, TEXT("flex-basis"), false, Layout },
			{ EWebToUECssProperty::JustifyContent, TEXT("justify-content"), false, Layout },
			{ EWebToUECssProperty::AlignItems, TEXT("align-items"), false, Layout },
			{ EWebToUECssProperty::AlignSelf, TEXT("align-self"), false, Layout },
			{ EWebToUECssProperty::Color, TEXT("color"), true, Paint },
			{ EWebToUECssProperty::Background, TEXT("background"), false, Paint },
			{ EWebToUECssProperty::BackgroundColor, TEXT("background-color"), false, Paint },
			{ EWebToUECssProperty::Border, TEXT("border"), false, Layout },
			{ EWebToUECssProperty::BorderColor, TEXT("border-color"), false, Paint },
			{ EWebToUECssProperty::BorderWidth, TEXT("border-width"), false, Layout },
			{ EWebToUECssProperty::BorderStyle, TEXT("border-style"), false, Layout },
			{ EWebToUECssProperty::BorderRadius, TEXT("border-radius"), false, Paint },
			{ EWebToUECssProperty::Opacity, TEXT("opacity"), false, Paint },
			{ EWebToUECssProperty::FontFamily, TEXT("font-family"), true, MeasureResource },
			{ EWebToUECssProperty::FontSize, TEXT("font-size"), true, Measure },
			{ EWebToUECssProperty::FontWeight, TEXT("font-weight"), true, MeasureResource },
			{ EWebToUECssProperty::TextAlign, TEXT("text-align"), true, Paint },
			{ EWebToUECssProperty::WhiteSpace, TEXT("white-space"), true, Measure },
			{ EWebToUECssProperty::ObjectFit, TEXT("object-fit"), false, Paint },
			{ EWebToUECssProperty::ZIndex, TEXT("z-index"), false, PaintHitTest },
			{ EWebToUECssProperty::Transform, TEXT("transform"), false, PaintHitTest },
			{ EWebToUECssProperty::TransformOrigin, TEXT("transform-origin"), false, PaintHitTest },
			{ EWebToUECssProperty::Transition, TEXT("transition"), false, StyleImpact }
		};

		static_assert(UE_ARRAY_COUNT(PropertyMetadata) ==
			static_cast<uint8>(EWebToUECssProperty::Transition) + 1,
			"Every serialized CSS property ID must have exactly one metadata entry.");
	}

	const FWebToUECssPropertyMetadata& GetCssPropertyMetadata(EWebToUECssProperty Property)
	{
		const uint8 PropertyIndex = static_cast<uint8>(Property);
		return PropertyIndex < UE_ARRAY_COUNT(PropertyMetadata)
			? PropertyMetadata[PropertyIndex] : PropertyMetadata[0];
	}

	TConstArrayView<FWebToUECssPropertyMetadata> GetAllCssPropertyMetadata()
	{
		return MakeArrayView(PropertyMetadata).RightChop(1);
	}

	static bool TryParseLength(const FString& Raw, FWebToUELength& OutLength)
	{
		FString Value = Raw.TrimStartAndEnd().ToLower();
		if (Value == TEXT("auto"))
		{
			OutLength = FWebToUELength::Auto();
			return true;
		}
		if (Value.EndsWith(TEXT("%")))
		{
			const FString Number = Value.LeftChop(1);
			if (!Number.IsNumeric()) return false;
			OutLength = FWebToUELength::Percent(FCString::Atof(*Number));
			return true;
		}
		if (Value.EndsWith(TEXT("px"))) Value.LeftChopInline(2);
		if (!Value.IsNumeric() && Value != TEXT("0")) return false;
		OutLength = FWebToUELength::Pixels(FCString::Atof(*Value));
		return true;
	}

	static bool TryParseColor(FString Raw, FLinearColor& OutColor)
	{
		Raw = Raw.TrimStartAndEnd().ToLower();
		if (Raw == TEXT("transparent")) { OutColor = FLinearColor::Transparent; return true; }
		if (Raw == TEXT("white")) { OutColor = FLinearColor::White; return true; }
		if (Raw == TEXT("black")) { OutColor = FLinearColor::Black; return true; }
		if (Raw == TEXT("red")) { OutColor = FLinearColor::Red; return true; }
		if (Raw == TEXT("green")) { OutColor = FLinearColor::Green; return true; }
		if (Raw == TEXT("blue")) { OutColor = FLinearColor::Blue; return true; }
		if (!Raw.StartsWith(TEXT("#"))) return false;

		FString Hex = Raw.Mid(1);
		if (Hex.Len() == 3 || Hex.Len() == 4)
		{
			FString Expanded;
			for (TCHAR Char : Hex) { Expanded.AppendChar(Char); Expanded.AppendChar(Char); }
			Hex = MoveTemp(Expanded);
		}
		if (Hex.Len() == 6) Hex += TEXT("ff");
		if (Hex.Len() != 8) return false;
		for (TCHAR Char : Hex)
		{
			if (!FChar::IsHexDigit(Char)) return false;
		}
		const uint32 RGBA = static_cast<uint32>(FCString::Strtoui64(*Hex, nullptr, 16));
		OutColor = FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>((RGBA >> 24) & 0xff),
			static_cast<uint8>((RGBA >> 16) & 0xff),
			static_cast<uint8>((RGBA >> 8) & 0xff),
			static_cast<uint8>(RGBA & 0xff)));
		return true;
	}

	static bool TryParseNumber(const FString& Raw, float& OutNumber)
	{
		double Parsed = 0.0;
		if (!LexTryParseString(Parsed, *Raw.TrimStartAndEnd())) return false;
		OutNumber = static_cast<float>(Parsed);
		return true;
	}

	static bool TryParseInteger(const FString& Raw, int32& OutInteger)
	{
		return LexTryParseString(OutInteger, *Raw.TrimStartAndEnd());
	}

	static bool TryParseTransitionTime(const FString& Raw, float& OutSeconds)
	{
		const FString Value = Raw.TrimStartAndEnd().ToLower();
		float Scale = 0.0f;
		FString Number;
		if (Value.EndsWith(TEXT("ms")))
		{
			Scale = 0.001f;
			Number = Value.LeftChop(2);
		}
		else if (Value.EndsWith(TEXT("s")))
		{
			Scale = 1.0f;
			Number = Value.LeftChop(1);
		}
		else
		{
			return false;
		}
		float Parsed = 0.0f;
		if (!TryParseNumber(Number, Parsed) || Parsed < 0.0f)
		{
			return false;
		}
		OutSeconds = Parsed * Scale;
		return FMath::IsFinite(OutSeconds);
	}

	static bool TryParseTransitionProperty(const FString& Raw,
		EWebToUECssProperty& OutProperty)
	{
		const FString Value = Raw.TrimStartAndEnd().ToLower();
		if (!TryGetCssProperty(Value, OutProperty)) return false;
		return OutProperty == EWebToUECssProperty::Opacity ||
			OutProperty == EWebToUECssProperty::Color ||
			OutProperty == EWebToUECssProperty::BackgroundColor ||
			OutProperty == EWebToUECssProperty::BorderColor ||
			OutProperty == EWebToUECssProperty::Transform;
	}

	static bool TryParseTransitionEasing(const FString& Raw,
		EWebToUETransitionEasing& OutEasing)
	{
		const FString Value = Raw.TrimStartAndEnd().ToLower();
		if (Value == TEXT("linear")) OutEasing = EWebToUETransitionEasing::Linear;
		else if (Value == TEXT("ease")) OutEasing = EWebToUETransitionEasing::Ease;
		else if (Value == TEXT("ease-in")) OutEasing = EWebToUETransitionEasing::EaseIn;
		else if (Value == TEXT("ease-out")) OutEasing = EWebToUETransitionEasing::EaseOut;
		else if (Value == TEXT("ease-in-out")) OutEasing = EWebToUETransitionEasing::EaseInOut;
		else return false;
		return true;
	}

	static bool TryParseTransition(const FString& Raw,
		FWebToUETransitionValue& OutTransition)
	{
		OutTransition.Items.Reset();
		TArray<FString> Items;
		Raw.ParseIntoArray(Items, TEXT(","), false);
		if (Items.IsEmpty()) return false;
		TSet<EWebToUECssProperty> Properties;
		for (const FString& RawItem : Items)
		{
			TArray<FString> Tokens;
			RawItem.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 2 || Tokens.Num() > 4) return false;
			FWebToUETransitionItem Item;
			if (!TryParseTransitionProperty(Tokens[0], Item.Property) ||
				Properties.Contains(Item.Property) ||
				!TryParseTransitionTime(Tokens[1], Item.DurationSeconds) ||
				Item.DurationSeconds <= 0.0f)
			{
				return false;
			}
			Properties.Add(Item.Property);
			bool bHaveEasing = false;
			bool bHaveDelay = false;
			for (int32 TokenIndex = 2; TokenIndex < Tokens.Num(); ++TokenIndex)
			{
				float Delay = 0.0f;
				EWebToUETransitionEasing Easing = EWebToUETransitionEasing::Ease;
				if (!bHaveEasing && TryParseTransitionEasing(Tokens[TokenIndex], Easing))
				{
					Item.Easing = Easing;
					bHaveEasing = true;
				}
				else if (!bHaveDelay && TryParseTransitionTime(Tokens[TokenIndex], Delay))
				{
					Item.DelaySeconds = Delay;
					bHaveDelay = true;
				}
				else
				{
					return false;
				}
			}
			OutTransition.Items.Add(Item);
		}
		return true;
	}

	static bool TryParseKeyword(FString Raw,
		std::initializer_list<TPair<const TCHAR*, EWebToUEStyleKeyword>> Allowed,
		EWebToUEStyleKeyword& OutKeyword)
	{
		Raw = Raw.TrimStartAndEnd().ToLower();
		for (const TPair<const TCHAR*, EWebToUEStyleKeyword>& Candidate : Allowed)
		{
			if (Raw == Candidate.Key)
			{
				OutKeyword = Candidate.Value;
				return true;
			}
		}
		return false;
	}

	static bool TryParseEdges(const FString& Raw, bool bAllowAuto, FWebToUEEdges& OutEdges)
	{
		TArray<FString> Parts;
		Raw.ParseIntoArrayWS(Parts);
		if (Parts.IsEmpty() || Parts.Num() > 4) return false;
		FWebToUELength Values[4];
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			if (!TryParseLength(Parts[Index], Values[Index]) ||
				(!bAllowAuto && Values[Index].Unit == EWebToUEUnit::Auto))
			{
				return false;
			}
		}
		const FWebToUELength A = Values[0];
		const FWebToUELength B = Parts.Num() > 1 ? Values[1] : A;
		const FWebToUELength C = Parts.Num() > 2 ? Values[2] : A;
		const FWebToUELength D = Parts.Num() > 3 ? Values[3] : B;
		OutEdges.Top = A;
		OutEdges.Right = B;
		OutEdges.Bottom = C;
		OutEdges.Left = D;
		return true;
	}

	static bool TryParseFlex(const FString& Raw, FWebToUEFlexValue& OutFlex)
	{
		TArray<FString> Parts;
		Raw.ParseIntoArrayWS(Parts);
		if (Parts.IsEmpty() || Parts.Num() > 3 || !TryParseNumber(Parts[0], OutFlex.Grow)) return false;
		OutFlex.bHasGrow = true;
		if (Parts.Num() == 1)
		{
			OutFlex.Shrink = 1.0f;
			OutFlex.Basis = FWebToUELength::Percent(0.0f);
			OutFlex.bHasShrink = true;
			OutFlex.bHasBasis = true;
			return true;
		}
		if (!TryParseNumber(Parts[1], OutFlex.Shrink)) return false;
		OutFlex.bHasShrink = true;
		if (Parts.Num() == 3)
		{
			if (!TryParseLength(Parts[2], OutFlex.Basis)) return false;
			OutFlex.bHasBasis = true;
		}
		return true;
	}

	static bool TryParseBorder(const FString& Raw, FWebToUEBorderValue& OutBorder)
	{
		TArray<FString> Parts;
		Raw.ParseIntoArrayWS(Parts);
		if (Parts.IsEmpty()) return false;
		for (const FString& Part : Parts)
		{
			FWebToUELength Length;
			FLinearColor Color;
			if (TryParseLength(Part, Length) && Length.Unit == EWebToUEUnit::Pixels)
			{
				OutBorder.Width = Length.Value;
				OutBorder.bHasWidth = true;
			}
			else if (TryParseColor(Part, Color))
			{
				OutBorder.Color = Color;
				OutBorder.bHasColor = true;
			}
			else if (Part.Equals(TEXT("none"), ESearchCase::IgnoreCase))
			{
				OutBorder.Width = 0.0f;
				OutBorder.bHasWidth = true;
			}
			else if (!Part.Equals(TEXT("solid"), ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
		return true;
	}

	static FVector2f TransformSymbolicVector(
		const FWebToUEVisualTransformValue& Transform, const FVector2f& Vector)
	{
		return FVector2f(
			Vector.X * Transform.M00 + Vector.Y * Transform.M10,
			Vector.X * Transform.M01 + Vector.Y * Transform.M11);
	}

	/** Return the affine result of applying A first and B second. */
	static FWebToUEVisualTransformValue ConcatenateTransform(
		const FWebToUEVisualTransformValue& A,
		const FWebToUEVisualTransformValue& B)
	{
		FWebToUEVisualTransformValue Result;
		Result.M00 = A.M00 * B.M00 + A.M01 * B.M10;
		Result.M01 = A.M00 * B.M01 + A.M01 * B.M11;
		Result.M10 = A.M10 * B.M00 + A.M11 * B.M10;
		Result.M11 = A.M10 * B.M01 + A.M11 * B.M11;
		Result.TranslationPixels =
			TransformSymbolicVector(B, A.TranslationPixels) + B.TranslationPixels;
		Result.TranslationByWidth =
			TransformSymbolicVector(B, A.TranslationByWidth) + B.TranslationByWidth;
		Result.TranslationByHeight =
			TransformSymbolicVector(B, A.TranslationByHeight) + B.TranslationByHeight;
		return Result;
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

	static bool TryParseTransformLength(const FString& Raw, FWebToUELength& OutLength)
	{
		const FString Value = Raw.TrimStartAndEnd().ToLower();
		if (!Value.EndsWith(TEXT("px")) && !Value.EndsWith(TEXT("%")))
		{
			return false;
		}
		return TryParseLength(Value, OutLength) && OutLength.Unit != EWebToUEUnit::Auto;
	}

	static void SetTranslateComponent(FWebToUEVisualTransformValue& Transform,
		const FWebToUELength& Length, bool bXAxis)
	{
		if (Length.Unit == EWebToUEUnit::Pixels)
		{
			if (bXAxis) Transform.TranslationPixels.X = Length.Value;
			else Transform.TranslationPixels.Y = Length.Value;
		}
		else if (Length.Unit == EWebToUEUnit::Percent)
		{
			if (bXAxis) Transform.TranslationByWidth.X = Length.Value * 0.01f;
			else Transform.TranslationByHeight.Y = Length.Value * 0.01f;
		}
	}

	static bool TryParseTransformFunction(const FString& FunctionName,
		FString Arguments, FWebToUEVisualTransformValue& OutTransform)
	{
		bool bHasTokenSinceComma = false;
		for (const TCHAR Char : Arguments)
		{
			if (Char == TEXT(','))
			{
				if (!bHasTokenSinceComma) return false;
				bHasTokenSinceComma = false;
			}
			else if (!FChar::IsWhitespace(Char))
			{
				bHasTokenSinceComma = true;
			}
		}
		if (!bHasTokenSinceComma) return false;
		Arguments.ReplaceInline(TEXT(","), TEXT(" "));
		TArray<FString> Parts;
		Arguments.ParseIntoArrayWS(Parts);
		const FString Name = FunctionName.ToLower();
		if (Name == TEXT("translate") || Name == TEXT("translatex") ||
			Name == TEXT("translatey"))
		{
			const int32 MaxParts = Name == TEXT("translate") ? 2 : 1;
			if (Parts.IsEmpty() || Parts.Num() > MaxParts) return false;
			FWebToUELength First;
			if (!TryParseTransformLength(Parts[0], First)) return false;
			if (Name == TEXT("translatey")) SetTranslateComponent(OutTransform, First, false);
			else SetTranslateComponent(OutTransform, First, true);
			if (Name == TEXT("translate") && Parts.Num() == 2)
			{
				FWebToUELength Second;
				if (!TryParseTransformLength(Parts[1], Second)) return false;
				SetTranslateComponent(OutTransform, Second, false);
			}
			return true;
		}
		if (Name == TEXT("scale") || Name == TEXT("scalex") || Name == TEXT("scaley"))
		{
			const int32 MaxParts = Name == TEXT("scale") ? 2 : 1;
			if (Parts.IsEmpty() || Parts.Num() > MaxParts) return false;
			float First = 0.0f;
			if (!TryParseNumber(Parts[0], First) || !FMath::IsFinite(First)) return false;
			float ScaleX = 1.0f;
			float ScaleY = 1.0f;
			if (Name == TEXT("scalex")) ScaleX = First;
			else if (Name == TEXT("scaley")) ScaleY = First;
			else
			{
				ScaleX = First;
				ScaleY = First;
				if (Parts.Num() == 2 &&
					(!TryParseNumber(Parts[1], ScaleY) || !FMath::IsFinite(ScaleY)))
				{
					return false;
				}
			}
			OutTransform.M00 = ScaleX;
			OutTransform.M11 = ScaleY;
			return true;
		}
		if (Name == TEXT("rotate"))
		{
			if (Parts.Num() != 1) return false;
			FString Angle = Parts[0].ToLower();
			if (!Angle.EndsWith(TEXT("deg"))) return false;
			Angle.LeftChopInline(3);
			float Degrees = 0.0f;
			if (!TryParseNumber(Angle, Degrees) || !FMath::IsFinite(Degrees)) return false;
			const float Radians = FMath::DegreesToRadians(Degrees);
			const float Cos = FMath::Cos(Radians);
			const float Sin = FMath::Sin(Radians);
			OutTransform.M00 = Cos;
			OutTransform.M01 = Sin;
			OutTransform.M10 = -Sin;
			OutTransform.M11 = Cos;
			return true;
		}
		return false;
	}

	static bool TryParseVisualTransform(FString Raw,
		FWebToUEVisualTransformValue& OutTransform)
	{
		Raw = Raw.TrimStartAndEnd();
		if (Raw.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			OutTransform = FWebToUEVisualTransformValue();
			return true;
		}
		if (Raw.IsEmpty()) return false;
		FWebToUEVisualTransformValue Combined;
		int32 Cursor = 0;
		int32 FunctionCount = 0;
		while (Cursor < Raw.Len())
		{
			while (Cursor < Raw.Len() && FChar::IsWhitespace(Raw[Cursor])) ++Cursor;
			const int32 NameStart = Cursor;
			while (Cursor < Raw.Len() && FChar::IsAlpha(Raw[Cursor])) ++Cursor;
			if (Cursor == NameStart || Cursor >= Raw.Len() || Raw[Cursor] != TEXT('(')) return false;
			const FString Name = Raw.Mid(NameStart, Cursor - NameStart);
			const int32 ArgumentsStart = ++Cursor;
			while (Cursor < Raw.Len() && Raw[Cursor] != TEXT(')'))
			{
				if (Raw[Cursor] == TEXT('(')) return false;
				++Cursor;
			}
			if (Cursor >= Raw.Len()) return false;
			FWebToUEVisualTransformValue Operation;
			if (!TryParseTransformFunction(Name,
				Raw.Mid(ArgumentsStart, Cursor - ArgumentsStart), Operation))
			{
				return false;
			}
			// CSS transform functions compose from right to left for points.
			Combined = ConcatenateTransform(Operation, Combined);
			++FunctionCount;
			++Cursor;
		}
		if (FunctionCount == 0 || !IsFiniteTransform(Combined)) return false;
		OutTransform = Combined;
		return true;
	}

	static bool TryParseOriginToken(const FString& Raw, bool bXAxis,
		FWebToUELength& OutLength)
	{
		const FString Token = Raw.TrimStartAndEnd().ToLower();
		if (Token == TEXT("center"))
		{
			OutLength = FWebToUELength::Percent(50.0f);
			return true;
		}
		if (bXAxis && Token == TEXT("left"))
		{
			OutLength = FWebToUELength::Percent(0.0f);
			return true;
		}
		if (bXAxis && Token == TEXT("right"))
		{
			OutLength = FWebToUELength::Percent(100.0f);
			return true;
		}
		if (!bXAxis && Token == TEXT("top"))
		{
			OutLength = FWebToUELength::Percent(0.0f);
			return true;
		}
		if (!bXAxis && Token == TEXT("bottom"))
		{
			OutLength = FWebToUELength::Percent(100.0f);
			return true;
		}
		return TryParseTransformLength(Token, OutLength);
	}

	static bool TryParseTransformOrigin(const FString& Raw,
		FWebToUETransformOriginValue& OutOrigin)
	{
		TArray<FString> Parts;
		Raw.ParseIntoArrayWS(Parts);
		if (Parts.IsEmpty() || Parts.Num() > 2) return false;
		OutOrigin = FWebToUETransformOriginValue();
		if (Parts.Num() == 1)
		{
			const FString Token = Parts[0].ToLower();
			if (Token == TEXT("top") || Token == TEXT("bottom"))
			{
				return TryParseOriginToken(Token, false, OutOrigin.Y);
			}
			return TryParseOriginToken(Token, true, OutOrigin.X);
		}
		if ((Parts[0].Equals(TEXT("top"), ESearchCase::IgnoreCase) ||
			 Parts[0].Equals(TEXT("bottom"), ESearchCase::IgnoreCase)) &&
			(Parts[1].Equals(TEXT("left"), ESearchCase::IgnoreCase) ||
			 Parts[1].Equals(TEXT("right"), ESearchCase::IgnoreCase) ||
			 Parts[1].Equals(TEXT("center"), ESearchCase::IgnoreCase)))
		{
			Swap(Parts[0], Parts[1]);
		}
		return TryParseOriginToken(Parts[0], true, OutOrigin.X) &&
			TryParseOriginToken(Parts[1], false, OutOrigin.Y);
	}

	bool TryGetCssProperty(const FString& Name, EWebToUECssProperty& OutProperty)
	{
		static const TMap<FString, EWebToUECssProperty> Properties = []
		{
			TMap<FString, EWebToUECssProperty> Result;
			Result.Reserve(GetAllCssPropertyMetadata().Num());
			for (const FWebToUECssPropertyMetadata& Metadata : GetAllCssPropertyMetadata())
			{
				Result.Add(Metadata.Name, Metadata.Property);
			}
			return Result;
		}();
		const FString CanonicalName = Name.TrimStartAndEnd().ToLower();
		const EWebToUECssProperty* Property = Properties.Find(CanonicalName);
		if (!Property) return false;
		OutProperty = *Property;
		return true;
	}

	const TCHAR* LexToString(EWebToUECssProperty Property)
	{
		return GetCssPropertyMetadata(Property).Name;
	}

	static bool TryKeywordValue(const FString& Raw,
		std::initializer_list<TPair<const TCHAR*, EWebToUEStyleKeyword>> Allowed,
		FWebToUEStyleValue& OutValue)
	{
		if (!TryParseKeyword(Raw, Allowed, OutValue.Keyword)) return false;
		OutValue.Type = EWebToUEStyleValueType::Keyword;
		return true;
	}

	bool TryParseCssValue(EWebToUECssProperty Property, const FString& Value, FWebToUEStyleValue& OutValue)
	{
		OutValue = FWebToUEStyleValue();
		if (Value.TrimStartAndEnd().IsEmpty()) return false;
		switch (Property)
		{
		case EWebToUECssProperty::Display:
			return TryKeywordValue(Value, {{TEXT("flex"), EWebToUEStyleKeyword::Flex}, {TEXT("none"), EWebToUEStyleKeyword::None}}, OutValue);
		case EWebToUECssProperty::Position:
			return TryKeywordValue(Value, {{TEXT("relative"), EWebToUEStyleKeyword::Relative}, {TEXT("absolute"), EWebToUEStyleKeyword::Absolute}}, OutValue);
		case EWebToUECssProperty::Visibility:
			return TryKeywordValue(Value, {{TEXT("visible"), EWebToUEStyleKeyword::Visible}, {TEXT("hidden"), EWebToUEStyleKeyword::Hidden}}, OutValue);
		case EWebToUECssProperty::Overflow:
			return TryKeywordValue(Value, {{TEXT("visible"), EWebToUEStyleKeyword::Visible}, {TEXT("hidden"), EWebToUEStyleKeyword::Hidden}, {TEXT("auto"), EWebToUEStyleKeyword::Auto}, {TEXT("scroll"), EWebToUEStyleKeyword::Scroll}}, OutValue);
		case EWebToUECssProperty::FlexDirection:
			return TryKeywordValue(Value, {{TEXT("row"), EWebToUEStyleKeyword::Row}, {TEXT("row-reverse"), EWebToUEStyleKeyword::RowReverse}, {TEXT("column"), EWebToUEStyleKeyword::Column}, {TEXT("column-reverse"), EWebToUEStyleKeyword::ColumnReverse}}, OutValue);
		case EWebToUECssProperty::FlexWrap:
			return TryKeywordValue(Value, {{TEXT("nowrap"), EWebToUEStyleKeyword::NoWrap}, {TEXT("wrap"), EWebToUEStyleKeyword::Wrap}, {TEXT("wrap-reverse"), EWebToUEStyleKeyword::WrapReverse}}, OutValue);
		case EWebToUECssProperty::JustifyContent:
			return TryKeywordValue(Value, {{TEXT("flex-start"), EWebToUEStyleKeyword::FlexStart}, {TEXT("center"), EWebToUEStyleKeyword::Center}, {TEXT("flex-end"), EWebToUEStyleKeyword::FlexEnd}, {TEXT("space-between"), EWebToUEStyleKeyword::SpaceBetween}, {TEXT("space-around"), EWebToUEStyleKeyword::SpaceAround}, {TEXT("space-evenly"), EWebToUEStyleKeyword::SpaceEvenly}}, OutValue);
		case EWebToUECssProperty::AlignItems:
			return TryKeywordValue(Value, {{TEXT("flex-start"), EWebToUEStyleKeyword::FlexStart}, {TEXT("center"), EWebToUEStyleKeyword::Center}, {TEXT("flex-end"), EWebToUEStyleKeyword::FlexEnd}, {TEXT("stretch"), EWebToUEStyleKeyword::Stretch}, {TEXT("baseline"), EWebToUEStyleKeyword::Baseline}}, OutValue);
		case EWebToUECssProperty::AlignSelf:
			return TryKeywordValue(Value, {{TEXT("auto"), EWebToUEStyleKeyword::Auto}, {TEXT("flex-start"), EWebToUEStyleKeyword::FlexStart}, {TEXT("center"), EWebToUEStyleKeyword::Center}, {TEXT("flex-end"), EWebToUEStyleKeyword::FlexEnd}, {TEXT("stretch"), EWebToUEStyleKeyword::Stretch}, {TEXT("baseline"), EWebToUEStyleKeyword::Baseline}}, OutValue);
		case EWebToUECssProperty::BorderStyle:
			return TryKeywordValue(Value, {{TEXT("solid"), EWebToUEStyleKeyword::Solid}, {TEXT("none"), EWebToUEStyleKeyword::None}}, OutValue);
		case EWebToUECssProperty::TextAlign:
			return TryKeywordValue(Value, {{TEXT("left"), EWebToUEStyleKeyword::Left}, {TEXT("center"), EWebToUEStyleKeyword::Center}, {TEXT("right"), EWebToUEStyleKeyword::Right}}, OutValue);
		case EWebToUECssProperty::WhiteSpace:
			return TryKeywordValue(Value, {{TEXT("normal"), EWebToUEStyleKeyword::Normal}, {TEXT("nowrap"), EWebToUEStyleKeyword::NoWrap}}, OutValue);
		case EWebToUECssProperty::ObjectFit:
			return TryKeywordValue(Value, {{TEXT("fill"), EWebToUEStyleKeyword::Fill}, {TEXT("contain"), EWebToUEStyleKeyword::Contain}, {TEXT("cover"), EWebToUEStyleKeyword::Cover}}, OutValue);
		case EWebToUECssProperty::Flex:
			OutValue.Type = EWebToUEStyleValueType::Flex;
			return TryParseFlex(Value, OutValue.Flex);
		case EWebToUECssProperty::FlexGrow:
		case EWebToUECssProperty::FlexShrink:
		case EWebToUECssProperty::Opacity:
			OutValue.Type = EWebToUEStyleValueType::Number;
			return TryParseNumber(Value, OutValue.Number);
		case EWebToUECssProperty::ZIndex:
			OutValue.Type = EWebToUEStyleValueType::Integer;
			return TryParseInteger(Value, OutValue.Integer);
		case EWebToUECssProperty::Transform:
			OutValue.Type = EWebToUEStyleValueType::Transform;
			return TryParseVisualTransform(Value, OutValue.Transform);
		case EWebToUECssProperty::TransformOrigin:
			OutValue.Type = EWebToUEStyleValueType::TransformOrigin;
			return TryParseTransformOrigin(Value, OutValue.TransformOrigin);
		case EWebToUECssProperty::Transition:
			OutValue.Type = EWebToUEStyleValueType::Transition;
			return TryParseTransition(Value, OutValue.Transition);
		case EWebToUECssProperty::Color:
		case EWebToUECssProperty::Background:
		case EWebToUECssProperty::BackgroundColor:
		case EWebToUECssProperty::BorderColor:
			OutValue.Type = EWebToUEStyleValueType::Color;
			return TryParseColor(Value, OutValue.Color);
		case EWebToUECssProperty::Margin:
		case EWebToUECssProperty::Padding:
			OutValue.Type = EWebToUEStyleValueType::Edges;
			return TryParseEdges(Value, Property == EWebToUECssProperty::Margin, OutValue.Edges);
		case EWebToUECssProperty::Border:
			OutValue.Type = EWebToUEStyleValueType::Border;
			return TryParseBorder(Value, OutValue.Border);
		case EWebToUECssProperty::FontFamily:
			OutValue.Type = EWebToUEStyleValueType::String;
			OutValue.String = Value.TrimStartAndEnd();
			if (OutValue.String.Len() >= 2 &&
				((OutValue.String[0] == TEXT('"') && OutValue.String[OutValue.String.Len() - 1] == TEXT('"')) ||
				 (OutValue.String[0] == TEXT('\'') && OutValue.String[OutValue.String.Len() - 1] == TEXT('\''))))
			{
				OutValue.String = OutValue.String.Mid(1, OutValue.String.Len() - 2);
			}
			return true;
		case EWebToUECssProperty::FontWeight:
		{
			FString Weight = Value.TrimStartAndEnd().ToLower();
			int32 NumericWeight = 0;
			if (Weight != TEXT("normal") && Weight != TEXT("bold") &&
				(!TryParseInteger(Weight, NumericWeight) || NumericWeight < 100 || NumericWeight > 900 || NumericWeight % 100 != 0))
			{
				return false;
			}
			OutValue.Type = EWebToUEStyleValueType::String;
			OutValue.String = MoveTemp(Weight);
			return true;
		}
		default:
			OutValue.Type = EWebToUEStyleValueType::Length;
			if (!TryParseLength(Value, OutValue.Length)) return false;
			if ((Property == EWebToUECssProperty::Gap || Property == EWebToUECssProperty::RowGap ||
				 Property == EWebToUECssProperty::ColumnGap || Property == EWebToUECssProperty::BorderWidth ||
				 Property == EWebToUECssProperty::BorderRadius || Property == EWebToUECssProperty::FontSize) &&
				OutValue.Length.Unit != EWebToUEUnit::Pixels)
			{
				return false;
			}
			if (Property >= EWebToUECssProperty::PaddingLeft && Property <= EWebToUECssProperty::PaddingBottom &&
				OutValue.Length.Unit == EWebToUEUnit::Auto)
			{
				return false;
			}
			return true;
		}
	}

	bool TryParseCssDeclaration(const FString& Name, const FString& Value,
		FWebToUEStyleDeclaration& OutDeclaration)
	{
		EWebToUECssProperty Property = EWebToUECssProperty::Invalid;
		if (!TryGetCssProperty(Name, Property)) return false;
		FWebToUEStyleValue TypedValue;
		if (!TryParseCssValue(Property, Value, TypedValue)) return false;
		OutDeclaration.Property = Property;
		OutDeclaration.TypedValue = MoveTemp(TypedValue);
		OutDeclaration.Name = Name.TrimStartAndEnd().ToLower();
		OutDeclaration.Value = Value.TrimStartAndEnd();
		return true;
	}

	static const TCHAR* KeywordToString(EWebToUEStyleKeyword Keyword)
	{
		switch (Keyword)
		{
		case EWebToUEStyleKeyword::NoWrap: return TEXT("nowrap"); case EWebToUEStyleKeyword::Wrap: return TEXT("wrap");
		case EWebToUEStyleKeyword::WrapReverse: return TEXT("wrap-reverse"); case EWebToUEStyleKeyword::FlexStart: return TEXT("flex-start");
		case EWebToUEStyleKeyword::Center: return TEXT("center"); case EWebToUEStyleKeyword::FlexEnd: return TEXT("flex-end");
		case EWebToUEStyleKeyword::SpaceBetween: return TEXT("space-between"); case EWebToUEStyleKeyword::SpaceAround: return TEXT("space-around");
		case EWebToUEStyleKeyword::SpaceEvenly: return TEXT("space-evenly"); case EWebToUEStyleKeyword::Stretch: return TEXT("stretch");
		case EWebToUEStyleKeyword::Baseline: return TEXT("baseline"); case EWebToUEStyleKeyword::Auto: return TEXT("auto");
		case EWebToUEStyleKeyword::Left: return TEXT("left"); case EWebToUEStyleKeyword::Right: return TEXT("right");
		case EWebToUEStyleKeyword::Normal: return TEXT("normal"); case EWebToUEStyleKeyword::Fill: return TEXT("fill");
		case EWebToUEStyleKeyword::Contain: return TEXT("contain"); case EWebToUEStyleKeyword::Cover: return TEXT("cover");
		default: return TEXT("");
		}
	}

	static void ApplyProperty(EWebToUECssProperty Property, const FWebToUEStyleValue& Value,
		FWebToUEComputedStyle& Style)
	{
		switch (Property)
		{
		case EWebToUECssProperty::Display: Style.Display = Value.Keyword == EWebToUEStyleKeyword::None ? EWebToUEDisplay::None : EWebToUEDisplay::Flex; break;
		case EWebToUECssProperty::Position: Style.Position = Value.Keyword == EWebToUEStyleKeyword::Absolute ? EWebToUEPosition::Absolute : EWebToUEPosition::Relative; break;
		case EWebToUECssProperty::Visibility: Style.bVisible = Value.Keyword != EWebToUEStyleKeyword::Hidden; break;
		case EWebToUECssProperty::Overflow:
			Style.Overflow = Value.Keyword == EWebToUEStyleKeyword::Hidden ? EWebToUEOverflow::Hidden :
				Value.Keyword == EWebToUEStyleKeyword::Auto ? EWebToUEOverflow::Auto :
				Value.Keyword == EWebToUEStyleKeyword::Scroll ? EWebToUEOverflow::Scroll : EWebToUEOverflow::Visible; break;
		case EWebToUECssProperty::Width: Style.Width = Value.Length; break; case EWebToUECssProperty::Height: Style.Height = Value.Length; break;
		case EWebToUECssProperty::MinWidth: Style.MinWidth = Value.Length; break; case EWebToUECssProperty::MinHeight: Style.MinHeight = Value.Length; break;
		case EWebToUECssProperty::MaxWidth: Style.MaxWidth = Value.Length; break; case EWebToUECssProperty::MaxHeight: Style.MaxHeight = Value.Length; break;
		case EWebToUECssProperty::Left: Style.Inset.Left = Value.Length; break; case EWebToUECssProperty::Top: Style.Inset.Top = Value.Length; break;
		case EWebToUECssProperty::Right: Style.Inset.Right = Value.Length; break; case EWebToUECssProperty::Bottom: Style.Inset.Bottom = Value.Length; break;
		case EWebToUECssProperty::Margin: Style.Margin = Value.Edges; break; case EWebToUECssProperty::MarginLeft: Style.Margin.Left = Value.Length; break;
		case EWebToUECssProperty::MarginTop: Style.Margin.Top = Value.Length; break; case EWebToUECssProperty::MarginRight: Style.Margin.Right = Value.Length; break;
		case EWebToUECssProperty::MarginBottom: Style.Margin.Bottom = Value.Length; break; case EWebToUECssProperty::Padding: Style.Padding = Value.Edges; break;
		case EWebToUECssProperty::PaddingLeft: Style.Padding.Left = Value.Length; break; case EWebToUECssProperty::PaddingTop: Style.Padding.Top = Value.Length; break;
		case EWebToUECssProperty::PaddingRight: Style.Padding.Right = Value.Length; break; case EWebToUECssProperty::PaddingBottom: Style.Padding.Bottom = Value.Length; break;
		case EWebToUECssProperty::Gap: Style.RowGap = Style.ColumnGap = Value.Length.Value; break;
		case EWebToUECssProperty::RowGap: Style.RowGap = Value.Length.Value; break; case EWebToUECssProperty::ColumnGap: Style.ColumnGap = Value.Length.Value; break;
		case EWebToUECssProperty::Flex:
			if (Value.Flex.bHasGrow) Style.FlexGrow = Value.Flex.Grow;
			if (Value.Flex.bHasShrink) Style.FlexShrink = Value.Flex.Shrink;
			if (Value.Flex.bHasBasis) Style.FlexBasis = Value.Flex.Basis;
			break;
		case EWebToUECssProperty::FlexDirection:
			Style.FlexDirection = Value.Keyword == EWebToUEStyleKeyword::Row ? EWebToUEFlexDirection::Row :
				Value.Keyword == EWebToUEStyleKeyword::RowReverse ? EWebToUEFlexDirection::RowReverse :
				Value.Keyword == EWebToUEStyleKeyword::ColumnReverse ? EWebToUEFlexDirection::ColumnReverse : EWebToUEFlexDirection::Column; break;
		case EWebToUECssProperty::FlexWrap: Style.FlexWrap = KeywordToString(Value.Keyword); break;
		case EWebToUECssProperty::FlexGrow: Style.FlexGrow = Value.Number; break; case EWebToUECssProperty::FlexShrink: Style.FlexShrink = Value.Number; break;
		case EWebToUECssProperty::FlexBasis: Style.FlexBasis = Value.Length; break;
		case EWebToUECssProperty::JustifyContent: Style.JustifyContent = KeywordToString(Value.Keyword); break;
		case EWebToUECssProperty::AlignItems: Style.AlignItems = KeywordToString(Value.Keyword); break;
		case EWebToUECssProperty::AlignSelf: Style.AlignSelf = KeywordToString(Value.Keyword); break;
		case EWebToUECssProperty::Color: Style.Color = Value.Color; break;
		case EWebToUECssProperty::Background: Style.BackgroundColor = Value.Color; break;
		case EWebToUECssProperty::BackgroundColor: Style.BackgroundColor = Value.Color; break;
		case EWebToUECssProperty::Border:
			if (Value.Border.bHasWidth) Style.BorderWidth = Value.Border.Width;
			if (Value.Border.bHasColor) Style.BorderColor = Value.Border.Color;
			break;
		case EWebToUECssProperty::BorderColor: Style.BorderColor = Value.Color; break;
		case EWebToUECssProperty::BorderWidth: Style.BorderWidth = Value.Length.Value; break;
		case EWebToUECssProperty::BorderStyle: if (Value.Keyword == EWebToUEStyleKeyword::None) Style.BorderWidth = 0.0f; break;
		case EWebToUECssProperty::BorderRadius: Style.BorderRadius = Value.Length.Value; break;
		case EWebToUECssProperty::Opacity: Style.Opacity = FMath::Clamp(Value.Number, 0.0f, 1.0f); break;
		case EWebToUECssProperty::FontFamily: Style.FontFamily = Value.String; break;
		case EWebToUECssProperty::FontSize: Style.FontSize = FMath::Max(1.0f, Value.Length.Value); break;
		case EWebToUECssProperty::FontWeight: Style.FontWeight = Value.String; break;
		case EWebToUECssProperty::TextAlign: Style.TextAlign = KeywordToString(Value.Keyword); break;
		case EWebToUECssProperty::WhiteSpace: Style.WhiteSpace = KeywordToString(Value.Keyword); break;
		case EWebToUECssProperty::ObjectFit: Style.ObjectFit = KeywordToString(Value.Keyword); break;
		case EWebToUECssProperty::ZIndex: Style.ZIndex = Value.Integer; break;
		case EWebToUECssProperty::Transform: Style.Transform = Value.Transform; break;
		case EWebToUECssProperty::TransformOrigin: Style.TransformOrigin = Value.TransformOrigin; break;
		case EWebToUECssProperty::Transition: Style.Transition = Value.Transition; break;
		default: break;
		}
	}

	void ApplyInheritedProperties(const FWebToUEComputedStyle& ParentStyle,
		FWebToUEComputedStyle& Style)
	{
		for (const FWebToUECssPropertyMetadata& Metadata : GetAllCssPropertyMetadata())
		{
			if (!Metadata.bInherited) continue;
			switch (Metadata.Property)
			{
			case EWebToUECssProperty::Color: Style.Color = ParentStyle.Color; break;
			case EWebToUECssProperty::FontFamily: Style.FontFamily = ParentStyle.FontFamily; break;
			case EWebToUECssProperty::FontSize: Style.FontSize = ParentStyle.FontSize; break;
			case EWebToUECssProperty::FontWeight: Style.FontWeight = ParentStyle.FontWeight; break;
			case EWebToUECssProperty::TextAlign: Style.TextAlign = ParentStyle.TextAlign; break;
			case EWebToUECssProperty::WhiteSpace: Style.WhiteSpace = ParentStyle.WhiteSpace; break;
			default: checkNoEntry(); break;
			}
		}
	}

	void ApplyCascadedProperty(EWebToUECssProperty PropertySlot,
		EWebToUECssProperty SourceProperty, const FWebToUEStyleValue& Value,
		FWebToUEComputedStyle& Style)
	{
		if (PropertySlot == SourceProperty)
		{
			ApplyProperty(PropertySlot, Value, Style);
			return;
		}

		switch (PropertySlot)
		{
		case EWebToUECssProperty::MarginLeft: Style.Margin.Left = Value.Edges.Left; break;
		case EWebToUECssProperty::MarginTop: Style.Margin.Top = Value.Edges.Top; break;
		case EWebToUECssProperty::MarginRight: Style.Margin.Right = Value.Edges.Right; break;
		case EWebToUECssProperty::MarginBottom: Style.Margin.Bottom = Value.Edges.Bottom; break;
		case EWebToUECssProperty::PaddingLeft: Style.Padding.Left = Value.Edges.Left; break;
		case EWebToUECssProperty::PaddingTop: Style.Padding.Top = Value.Edges.Top; break;
		case EWebToUECssProperty::PaddingRight: Style.Padding.Right = Value.Edges.Right; break;
		case EWebToUECssProperty::PaddingBottom: Style.Padding.Bottom = Value.Edges.Bottom; break;
		case EWebToUECssProperty::RowGap: Style.RowGap = Value.Length.Value; break;
		case EWebToUECssProperty::ColumnGap: Style.ColumnGap = Value.Length.Value; break;
		case EWebToUECssProperty::FlexGrow: Style.FlexGrow = Value.Flex.Grow; break;
		case EWebToUECssProperty::FlexShrink: Style.FlexShrink = Value.Flex.Shrink; break;
		case EWebToUECssProperty::FlexBasis: Style.FlexBasis = Value.Flex.Basis; break;
		case EWebToUECssProperty::BackgroundColor: Style.BackgroundColor = Value.Color; break;
		case EWebToUECssProperty::BorderWidth: Style.BorderWidth = Value.Border.Width; break;
		case EWebToUECssProperty::BorderColor: Style.BorderColor = Value.Border.Color; break;
		default: checkNoEntry(); break;
		}
	}

	static bool EqualLength(const FWebToUELength& A, const FWebToUELength& B)
	{
		return A.Unit == B.Unit && FMath::IsNearlyEqual(A.Value, B.Value);
	}

	bool IsCanonicalComputedStyleProperty(EWebToUECssProperty Property)
	{
		switch (Property)
		{
		case EWebToUECssProperty::Margin:
		case EWebToUECssProperty::Padding:
		case EWebToUECssProperty::Gap:
		case EWebToUECssProperty::Flex:
		case EWebToUECssProperty::Background:
		case EWebToUECssProperty::Border:
		case EWebToUECssProperty::BorderStyle:
			return false;
		default:
			return Property != EWebToUECssProperty::Invalid;
		}
	}

	bool AreComputedStylePropertyValuesEqual(EWebToUECssProperty Property,
		const FWebToUEComputedStyle& A, const FWebToUEComputedStyle& B)
	{
		switch (Property)
		{
		case EWebToUECssProperty::Display: return A.Display == B.Display;
		case EWebToUECssProperty::Position: return A.Position == B.Position;
		case EWebToUECssProperty::Visibility: return A.bVisible == B.bVisible;
		case EWebToUECssProperty::Overflow: return A.Overflow == B.Overflow;
		case EWebToUECssProperty::Width: return EqualLength(A.Width, B.Width);
		case EWebToUECssProperty::Height: return EqualLength(A.Height, B.Height);
		case EWebToUECssProperty::MinWidth: return EqualLength(A.MinWidth, B.MinWidth);
		case EWebToUECssProperty::MinHeight: return EqualLength(A.MinHeight, B.MinHeight);
		case EWebToUECssProperty::MaxWidth: return EqualLength(A.MaxWidth, B.MaxWidth);
		case EWebToUECssProperty::MaxHeight: return EqualLength(A.MaxHeight, B.MaxHeight);
		case EWebToUECssProperty::Left: return EqualLength(A.Inset.Left, B.Inset.Left);
		case EWebToUECssProperty::Top: return EqualLength(A.Inset.Top, B.Inset.Top);
		case EWebToUECssProperty::Right: return EqualLength(A.Inset.Right, B.Inset.Right);
		case EWebToUECssProperty::Bottom: return EqualLength(A.Inset.Bottom, B.Inset.Bottom);
		case EWebToUECssProperty::MarginLeft: return EqualLength(A.Margin.Left, B.Margin.Left);
		case EWebToUECssProperty::MarginTop: return EqualLength(A.Margin.Top, B.Margin.Top);
		case EWebToUECssProperty::MarginRight: return EqualLength(A.Margin.Right, B.Margin.Right);
		case EWebToUECssProperty::MarginBottom: return EqualLength(A.Margin.Bottom, B.Margin.Bottom);
		case EWebToUECssProperty::PaddingLeft: return EqualLength(A.Padding.Left, B.Padding.Left);
		case EWebToUECssProperty::PaddingTop: return EqualLength(A.Padding.Top, B.Padding.Top);
		case EWebToUECssProperty::PaddingRight: return EqualLength(A.Padding.Right, B.Padding.Right);
		case EWebToUECssProperty::PaddingBottom: return EqualLength(A.Padding.Bottom, B.Padding.Bottom);
		case EWebToUECssProperty::RowGap: return FMath::IsNearlyEqual(A.RowGap, B.RowGap);
		case EWebToUECssProperty::ColumnGap: return FMath::IsNearlyEqual(A.ColumnGap, B.ColumnGap);
		case EWebToUECssProperty::FlexDirection: return A.FlexDirection == B.FlexDirection;
		case EWebToUECssProperty::FlexWrap: return A.FlexWrap == B.FlexWrap;
		case EWebToUECssProperty::FlexGrow: return FMath::IsNearlyEqual(A.FlexGrow, B.FlexGrow);
		case EWebToUECssProperty::FlexShrink: return FMath::IsNearlyEqual(A.FlexShrink, B.FlexShrink);
		case EWebToUECssProperty::FlexBasis: return EqualLength(A.FlexBasis, B.FlexBasis);
		case EWebToUECssProperty::JustifyContent: return A.JustifyContent == B.JustifyContent;
		case EWebToUECssProperty::AlignItems: return A.AlignItems == B.AlignItems;
		case EWebToUECssProperty::AlignSelf: return A.AlignSelf == B.AlignSelf;
		case EWebToUECssProperty::Color: return A.Color.Equals(B.Color);
		case EWebToUECssProperty::BackgroundColor: return A.BackgroundColor.Equals(B.BackgroundColor);
		case EWebToUECssProperty::BorderColor: return A.BorderColor.Equals(B.BorderColor);
		case EWebToUECssProperty::BorderWidth: return FMath::IsNearlyEqual(A.BorderWidth, B.BorderWidth);
		case EWebToUECssProperty::BorderRadius: return FMath::IsNearlyEqual(A.BorderRadius, B.BorderRadius);
		case EWebToUECssProperty::Opacity: return FMath::IsNearlyEqual(A.Opacity, B.Opacity);
		case EWebToUECssProperty::FontFamily: return A.FontFamily == B.FontFamily;
		case EWebToUECssProperty::FontSize: return FMath::IsNearlyEqual(A.FontSize, B.FontSize);
		case EWebToUECssProperty::FontWeight: return A.FontWeight == B.FontWeight;
		case EWebToUECssProperty::TextAlign: return A.TextAlign == B.TextAlign;
		case EWebToUECssProperty::WhiteSpace: return A.WhiteSpace == B.WhiteSpace;
		case EWebToUECssProperty::ObjectFit: return A.ObjectFit == B.ObjectFit;
		case EWebToUECssProperty::ZIndex: return A.ZIndex == B.ZIndex;
		case EWebToUECssProperty::Transform:
			return FMath::IsNearlyEqual(A.Transform.M00, B.Transform.M00) &&
				FMath::IsNearlyEqual(A.Transform.M01, B.Transform.M01) &&
				FMath::IsNearlyEqual(A.Transform.M10, B.Transform.M10) &&
				FMath::IsNearlyEqual(A.Transform.M11, B.Transform.M11) &&
				A.Transform.TranslationPixels.Equals(B.Transform.TranslationPixels) &&
				A.Transform.TranslationByWidth.Equals(B.Transform.TranslationByWidth) &&
				A.Transform.TranslationByHeight.Equals(B.Transform.TranslationByHeight);
		case EWebToUECssProperty::TransformOrigin:
			return EqualLength(A.TransformOrigin.X, B.TransformOrigin.X) &&
				EqualLength(A.TransformOrigin.Y, B.TransformOrigin.Y);
		case EWebToUECssProperty::Transition: return A.Transition == B.Transition;
		default: return true;
		}
	}
}
