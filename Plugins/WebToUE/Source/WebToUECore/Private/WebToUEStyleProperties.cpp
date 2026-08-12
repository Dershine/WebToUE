#include "WebToUEStyleProperties.h"

#include "String/LexFromString.h"

#include <initializer_list>

namespace WebToUE::Private
{
	static FWebToUELength ParseLength(const FString& Raw)
	{
		FString Value = Raw.TrimStartAndEnd().ToLower();
		if (Value == TEXT("auto")) return FWebToUELength::Auto();
		if (Value.EndsWith(TEXT("%")))
		{
			const FString Number = Value.LeftChop(1);
			return Number.IsNumeric() ? FWebToUELength::Percent(FCString::Atof(*Number)) : FWebToUELength();
		}
		if (Value.EndsWith(TEXT("px"))) Value.LeftChopInline(2);
		if (Value.IsNumeric() || Value == TEXT("0")) return FWebToUELength::Pixels(FCString::Atof(*Value));
		return {};
	}

	static float ParsePixels(const FString& Raw, float Fallback = 0.0f)
	{
		const FWebToUELength Length = ParseLength(Raw);
		return Length.Unit == EWebToUEUnit::Pixels ? Length.Value : Fallback;
	}

	static bool ParseColor(FString Raw, FLinearColor& OutColor)
	{
		Raw = Raw.TrimStartAndEnd().ToLower();
		if (Raw == TEXT("transparent")) { OutColor = FLinearColor::Transparent; return true; }
		if (Raw == TEXT("white")) { OutColor = FLinearColor::White; return true; }
		if (Raw == TEXT("black")) { OutColor = FLinearColor::Black; return true; }
		if (Raw == TEXT("red")) { OutColor = FLinearColor::Red; return true; }
		if (Raw == TEXT("green")) { OutColor = FLinearColor::Green; return true; }
		if (Raw == TEXT("blue")) { OutColor = FLinearColor::Blue; return true; }
		if (Raw.StartsWith(TEXT("#")))
		{
			FString Hex = Raw.Mid(1);
			if (Hex.Len() == 3 || Hex.Len() == 4)
			{
				FString Expanded;
				for (TCHAR Char : Hex) { Expanded.AppendChar(Char); Expanded.AppendChar(Char); }
				Hex = MoveTemp(Expanded);
			}
			if (Hex.Len() == 6) Hex += TEXT("ff");
			bool bValidHex = Hex.Len() == 8;
			for (TCHAR Char : Hex)
			{
				bValidHex = bValidHex && FChar::IsHexDigit(Char);
			}
			if (bValidHex)
			{
				const uint32 RGBA = FCString::Strtoui64(*Hex, nullptr, 16);
				OutColor = FLinearColor(
					((RGBA >> 24) & 0xff) / 255.0f,
					((RGBA >> 16) & 0xff) / 255.0f,
					((RGBA >> 8) & 0xff) / 255.0f,
					(RGBA & 0xff) / 255.0f);
				return true;
			}
		}
		return false;
	}

	static void ParseEdges(const FString& Raw, FWebToUEEdges& Edges)
	{
		TArray<FString> Parts;
		Raw.ParseIntoArrayWS(Parts);
		if (Parts.Num() == 0 || Parts.Num() > 4) return;
		const FWebToUELength A = ParseLength(Parts[0]);
		const FWebToUELength B = Parts.Num() > 1 ? ParseLength(Parts[1]) : A;
		const FWebToUELength C = Parts.Num() > 2 ? ParseLength(Parts[2]) : A;
		const FWebToUELength D = Parts.Num() > 3 ? ParseLength(Parts[3]) : B;
		Edges.Top = A;
		Edges.Right = B;
		Edges.Bottom = C;
		Edges.Left = D;
	}

	static const TSet<FString>& GetKnownCssProperties()
	{
		static const TSet<FString> Properties = {
			TEXT("display"), TEXT("position"), TEXT("visibility"), TEXT("overflow"),
			TEXT("width"), TEXT("height"), TEXT("min-width"), TEXT("min-height"),
			TEXT("max-width"), TEXT("max-height"), TEXT("left"), TEXT("top"), TEXT("right"), TEXT("bottom"),
			TEXT("margin"), TEXT("margin-left"), TEXT("margin-top"), TEXT("margin-right"), TEXT("margin-bottom"),
			TEXT("padding"), TEXT("padding-left"), TEXT("padding-top"), TEXT("padding-right"), TEXT("padding-bottom"),
			TEXT("gap"), TEXT("row-gap"), TEXT("column-gap"),
			TEXT("flex"), TEXT("flex-direction"), TEXT("flex-wrap"), TEXT("flex-grow"), TEXT("flex-shrink"),
			TEXT("flex-basis"), TEXT("justify-content"), TEXT("align-items"), TEXT("align-self"),
			TEXT("color"), TEXT("background"), TEXT("background-color"),
			TEXT("border"), TEXT("border-color"), TEXT("border-width"), TEXT("border-style"), TEXT("border-radius"),
			TEXT("opacity"), TEXT("font-family"), TEXT("font-size"), TEXT("font-weight"), TEXT("text-align"),
			TEXT("white-space"), TEXT("object-fit"), TEXT("z-index")
		};
		return Properties;
	}

	bool IsKnownCssProperty(const FString& Name)
	{
		return GetKnownCssProperties().Contains(Name);
	}

	static bool IsNumber(const FString& Raw)
	{
		double Parsed = 0.0;
		return LexTryParseString(Parsed, *Raw.TrimStartAndEnd());
	}

	static bool IsInteger(const FString& Raw)
	{
		int32 Parsed = 0;
		return LexTryParseString(Parsed, *Raw.TrimStartAndEnd());
	}

	static bool IsOneOf(FString Value, std::initializer_list<const TCHAR*> Allowed)
	{
		Value = Value.TrimStartAndEnd().ToLower();
		for (const TCHAR* Candidate : Allowed)
		{
			if (Value == Candidate) return true;
		}
		return false;
	}

	static bool IsLength(const FString& Raw, bool bAllowAuto = true, bool bAllowPercent = true)
	{
		const FWebToUELength Length = ParseLength(Raw);
		return Length.Unit == EWebToUEUnit::Pixels ||
			(bAllowAuto && Length.Unit == EWebToUEUnit::Auto) ||
			(bAllowPercent && Length.Unit == EWebToUEUnit::Percent);
	}

	static bool AreEdgesValid(const FString& Raw, bool bAllowAuto)
	{
		TArray<FString> Parts;
		Raw.ParseIntoArrayWS(Parts);
		if (Parts.IsEmpty() || Parts.Num() > 4) return false;
		return Parts.ContainsByPredicate([bAllowAuto](const FString& Part)
		{
			return !IsLength(Part, bAllowAuto);
		}) == false;
	}

	static bool IsBorderValid(const FString& Raw)
	{
		TArray<FString> Parts;
		Raw.ParseIntoArrayWS(Parts);
		if (Parts.IsEmpty()) return false;
		for (const FString& Part : Parts)
		{
			FLinearColor Color;
			if (IsLength(Part, false, false) || ParseColor(Part, Color) || IsOneOf(Part, { TEXT("solid"), TEXT("none") }))
			{
				continue;
			}
			return false;
		}
		return true;
	}

	static bool IsFlexValid(const FString& Raw)
	{
		TArray<FString> Parts;
		Raw.ParseIntoArrayWS(Parts);
		if (Parts.IsEmpty() || Parts.Num() > 3) return false;
		if (!IsNumber(Parts[0])) return false;
		if (Parts.Num() > 1 && !IsNumber(Parts[1])) return false;
		return Parts.Num() < 3 || IsLength(Parts[2]);
	}

	static bool IsFontWeightValid(const FString& Raw)
	{
		if (IsOneOf(Raw, { TEXT("normal"), TEXT("bold") })) return true;
		int32 Weight = 0;
		return LexTryParseString(Weight, *Raw.TrimStartAndEnd()) && Weight >= 100 && Weight <= 900 && Weight % 100 == 0;
	}

	bool IsValidCssValue(const FString& Name, const FString& Value)
	{
		if (Value.TrimStartAndEnd().IsEmpty()) return false;
		if (Name == TEXT("display")) return IsOneOf(Value, { TEXT("flex"), TEXT("none") });
		if (Name == TEXT("position")) return IsOneOf(Value, { TEXT("relative"), TEXT("absolute") });
		if (Name == TEXT("visibility")) return IsOneOf(Value, { TEXT("visible"), TEXT("hidden") });
		if (Name == TEXT("overflow")) return IsOneOf(Value, { TEXT("visible"), TEXT("hidden"), TEXT("auto"), TEXT("scroll") });
		if (Name == TEXT("flex-direction")) return IsOneOf(Value,
			{ TEXT("row"), TEXT("row-reverse"), TEXT("column"), TEXT("column-reverse") });
		if (Name == TEXT("flex-wrap")) return IsOneOf(Value, { TEXT("nowrap"), TEXT("wrap"), TEXT("wrap-reverse") });
		if (Name == TEXT("justify-content")) return IsOneOf(Value,
			{ TEXT("flex-start"), TEXT("center"), TEXT("flex-end"), TEXT("space-between"), TEXT("space-around"), TEXT("space-evenly") });
		if (Name == TEXT("align-items")) return IsOneOf(Value,
			{ TEXT("flex-start"), TEXT("center"), TEXT("flex-end"), TEXT("stretch"), TEXT("baseline") });
		if (Name == TEXT("align-self")) return IsOneOf(Value,
			{ TEXT("auto"), TEXT("flex-start"), TEXT("center"), TEXT("flex-end"), TEXT("stretch"), TEXT("baseline") });
		if (Name == TEXT("border-style")) return IsOneOf(Value, { TEXT("solid"), TEXT("none") });
		if (Name == TEXT("text-align")) return IsOneOf(Value, { TEXT("left"), TEXT("center"), TEXT("right") });
		if (Name == TEXT("white-space")) return IsOneOf(Value, { TEXT("normal"), TEXT("nowrap") });
		if (Name == TEXT("object-fit")) return IsOneOf(Value, { TEXT("fill"), TEXT("contain"), TEXT("cover") });
		if (Name == TEXT("flex")) return IsFlexValid(Value);
		if (Name == TEXT("flex-grow") || Name == TEXT("flex-shrink") || Name == TEXT("opacity")) return IsNumber(Value);
		if (Name == TEXT("z-index")) return IsInteger(Value);
		if (Name == TEXT("font-weight")) return IsFontWeightValid(Value);
		if (Name == TEXT("font-family")) return true;
		if (Name == TEXT("color") || Name == TEXT("background") || Name == TEXT("background-color") || Name == TEXT("border-color"))
		{
			FLinearColor Color;
			return ParseColor(Value, Color);
		}
		if (Name == TEXT("border")) return IsBorderValid(Value);
		if (Name == TEXT("margin")) return AreEdgesValid(Value, true);
		if (Name == TEXT("padding")) return AreEdgesValid(Value, false);
		if (Name.StartsWith(TEXT("margin-"))) return IsLength(Value, true);
		if (Name.StartsWith(TEXT("padding-"))) return IsLength(Value, false);
		if (Name == TEXT("gap") || Name == TEXT("row-gap") || Name == TEXT("column-gap") ||
			Name == TEXT("border-width") || Name == TEXT("border-radius") || Name == TEXT("font-size"))
		{
			return IsLength(Value, false, false);
		}
		return IsLength(Value);
	}

	void ApplyProperties(const TMap<FString, FString>& Properties, FWebToUEComputedStyle& Style)
	{
		auto Value = [&Properties](const TCHAR* Name) -> const FString* { return Properties.Find(Name); };
		if (const FString* V = Value(TEXT("flex")))
		{
			TArray<FString> Parts;
			V->ParseIntoArrayWS(Parts);
			if (Parts.Num() == 1 && Parts[0].IsNumeric())
			{
				Style.FlexGrow = FCString::Atof(*Parts[0]);
				Style.FlexShrink = 1.0f;
				Style.FlexBasis = FWebToUELength::Percent(0.0f);
			}
			else
			{
				if (Parts.Num() > 0 && Parts[0].IsNumeric()) Style.FlexGrow = FCString::Atof(*Parts[0]);
				if (Parts.Num() > 1 && Parts[1].IsNumeric()) Style.FlexShrink = FCString::Atof(*Parts[1]);
				if (Parts.Num() > 2) Style.FlexBasis = ParseLength(Parts[2]);
			}
		}
		if (const FString* V = Value(TEXT("border")))
		{
			TArray<FString> Parts;
			V->ParseIntoArrayWS(Parts);
			for (const FString& Part : Parts)
			{
				if (Part.Equals(TEXT("none"), ESearchCase::IgnoreCase))
				{
					Style.BorderWidth = 0.0f;
					continue;
				}
				const FWebToUELength Length = ParseLength(Part);
				if (Length.Unit == EWebToUEUnit::Pixels) Style.BorderWidth = Length.Value;
				else ParseColor(Part, Style.BorderColor);
			}
		}
		if (const FString* V = Value(TEXT("background"))) ParseColor(*V, Style.BackgroundColor);
		if (const FString* V = Value(TEXT("display"))) Style.Display = V->Equals(TEXT("none"), ESearchCase::IgnoreCase) ? EWebToUEDisplay::None : EWebToUEDisplay::Flex;
		if (const FString* V = Value(TEXT("position"))) Style.Position = V->Equals(TEXT("absolute"), ESearchCase::IgnoreCase) ? EWebToUEPosition::Absolute : EWebToUEPosition::Relative;
		if (const FString* V = Value(TEXT("overflow")))
		{
			if (V->Equals(TEXT("hidden"), ESearchCase::IgnoreCase)) Style.Overflow = EWebToUEOverflow::Hidden;
			else if (V->Equals(TEXT("auto"), ESearchCase::IgnoreCase)) Style.Overflow = EWebToUEOverflow::Auto;
			else if (V->Equals(TEXT("scroll"), ESearchCase::IgnoreCase)) Style.Overflow = EWebToUEOverflow::Scroll;
			else Style.Overflow = EWebToUEOverflow::Visible;
		}
		if (const FString* V = Value(TEXT("visibility"))) Style.bVisible = !V->Equals(TEXT("hidden"), ESearchCase::IgnoreCase);
		if (const FString* V = Value(TEXT("flex-direction")))
		{
			if (V->Equals(TEXT("row"), ESearchCase::IgnoreCase)) Style.FlexDirection = EWebToUEFlexDirection::Row;
			else if (V->Equals(TEXT("row-reverse"), ESearchCase::IgnoreCase)) Style.FlexDirection = EWebToUEFlexDirection::RowReverse;
			else if (V->Equals(TEXT("column-reverse"), ESearchCase::IgnoreCase)) Style.FlexDirection = EWebToUEFlexDirection::ColumnReverse;
			else Style.FlexDirection = EWebToUEFlexDirection::Column;
		}
		if (const FString* V = Value(TEXT("flex-wrap"))) Style.FlexWrap = V->ToLower();
		if (const FString* V = Value(TEXT("justify-content"))) Style.JustifyContent = V->ToLower();
		if (const FString* V = Value(TEXT("align-items"))) Style.AlignItems = V->ToLower();
		if (const FString* V = Value(TEXT("align-self"))) Style.AlignSelf = V->ToLower();
		if (const FString* V = Value(TEXT("flex-grow"))) Style.FlexGrow = FCString::Atof(**V);
		if (const FString* V = Value(TEXT("flex-shrink"))) Style.FlexShrink = FCString::Atof(**V);
		if (const FString* V = Value(TEXT("flex-basis"))) Style.FlexBasis = ParseLength(*V);
		if (const FString* V = Value(TEXT("width"))) Style.Width = ParseLength(*V);
		if (const FString* V = Value(TEXT("height"))) Style.Height = ParseLength(*V);
		if (const FString* V = Value(TEXT("min-width"))) Style.MinWidth = ParseLength(*V);
		if (const FString* V = Value(TEXT("min-height"))) Style.MinHeight = ParseLength(*V);
		if (const FString* V = Value(TEXT("max-width"))) Style.MaxWidth = ParseLength(*V);
		if (const FString* V = Value(TEXT("max-height"))) Style.MaxHeight = ParseLength(*V);
		if (const FString* V = Value(TEXT("margin"))) ParseEdges(*V, Style.Margin);
		if (const FString* V = Value(TEXT("padding"))) ParseEdges(*V, Style.Padding);
		if (const FString* V = Value(TEXT("margin-left"))) Style.Margin.Left = ParseLength(*V);
		if (const FString* V = Value(TEXT("margin-top"))) Style.Margin.Top = ParseLength(*V);
		if (const FString* V = Value(TEXT("margin-right"))) Style.Margin.Right = ParseLength(*V);
		if (const FString* V = Value(TEXT("margin-bottom"))) Style.Margin.Bottom = ParseLength(*V);
		if (const FString* V = Value(TEXT("padding-left"))) Style.Padding.Left = ParseLength(*V);
		if (const FString* V = Value(TEXT("padding-top"))) Style.Padding.Top = ParseLength(*V);
		if (const FString* V = Value(TEXT("padding-right"))) Style.Padding.Right = ParseLength(*V);
		if (const FString* V = Value(TEXT("padding-bottom"))) Style.Padding.Bottom = ParseLength(*V);
		if (const FString* V = Value(TEXT("left"))) Style.Inset.Left = ParseLength(*V);
		if (const FString* V = Value(TEXT("top"))) Style.Inset.Top = ParseLength(*V);
		if (const FString* V = Value(TEXT("right"))) Style.Inset.Right = ParseLength(*V);
		if (const FString* V = Value(TEXT("bottom"))) Style.Inset.Bottom = ParseLength(*V);
		if (const FString* V = Value(TEXT("gap"))) Style.RowGap = Style.ColumnGap = ParsePixels(*V);
		if (const FString* V = Value(TEXT("row-gap"))) Style.RowGap = ParsePixels(*V);
		if (const FString* V = Value(TEXT("column-gap"))) Style.ColumnGap = ParsePixels(*V);
		if (const FString* V = Value(TEXT("color"))) ParseColor(*V, Style.Color);
		if (const FString* V = Value(TEXT("background-color"))) ParseColor(*V, Style.BackgroundColor);
		if (const FString* V = Value(TEXT("border-color"))) ParseColor(*V, Style.BorderColor);
		if (const FString* V = Value(TEXT("border-width"))) Style.BorderWidth = ParsePixels(*V);
		if (const FString* V = Value(TEXT("border-style")); V && V->Equals(TEXT("none"), ESearchCase::IgnoreCase)) Style.BorderWidth = 0.0f;
		if (const FString* V = Value(TEXT("border-radius"))) Style.BorderRadius = ParsePixels(*V);
		if (const FString* V = Value(TEXT("opacity"))) Style.Opacity = FMath::Clamp(FCString::Atof(**V), 0.0f, 1.0f);
		if (const FString* V = Value(TEXT("font-family")))
		{
			Style.FontFamily = V->TrimStartAndEnd();
			if (Style.FontFamily.Len() >= 2 &&
				((Style.FontFamily[0] == TEXT('"') && Style.FontFamily[Style.FontFamily.Len() - 1] == TEXT('"')) ||
				 (Style.FontFamily[0] == TEXT('\'') && Style.FontFamily[Style.FontFamily.Len() - 1] == TEXT('\''))))
			{
				Style.FontFamily = Style.FontFamily.Mid(1, Style.FontFamily.Len() - 2);
			}
		}
		if (const FString* V = Value(TEXT("font-size"))) Style.FontSize = FMath::Max(1.0f, ParsePixels(*V, Style.FontSize));
		if (const FString* V = Value(TEXT("font-weight"))) Style.FontWeight = V->ToLower();
		if (const FString* V = Value(TEXT("text-align"))) Style.TextAlign = V->ToLower();
		if (const FString* V = Value(TEXT("white-space"))) Style.WhiteSpace = V->ToLower();
		if (const FString* V = Value(TEXT("object-fit"))) Style.ObjectFit = V->ToLower();
		if (const FString* V = Value(TEXT("z-index"))) Style.ZIndex = FCString::Atoi(**V);
	}
}
