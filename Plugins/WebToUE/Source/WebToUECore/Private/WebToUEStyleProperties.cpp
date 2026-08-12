#include "WebToUEStyleProperties.h"

#include "String/LexFromString.h"

#include <initializer_list>

namespace WebToUE::Private
{
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
		OutColor = FLinearColor(
			((RGBA >> 24) & 0xff) / 255.0f,
			((RGBA >> 16) & 0xff) / 255.0f,
			((RGBA >> 8) & 0xff) / 255.0f,
			(RGBA & 0xff) / 255.0f);
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

	bool TryGetCssProperty(const FString& Name, EWebToUECssProperty& OutProperty)
	{
		static const TMap<FString, EWebToUECssProperty> Properties = {
			{ TEXT("display"), EWebToUECssProperty::Display },
			{ TEXT("position"), EWebToUECssProperty::Position },
			{ TEXT("visibility"), EWebToUECssProperty::Visibility },
			{ TEXT("overflow"), EWebToUECssProperty::Overflow },
			{ TEXT("width"), EWebToUECssProperty::Width }, { TEXT("height"), EWebToUECssProperty::Height },
			{ TEXT("min-width"), EWebToUECssProperty::MinWidth }, { TEXT("min-height"), EWebToUECssProperty::MinHeight },
			{ TEXT("max-width"), EWebToUECssProperty::MaxWidth }, { TEXT("max-height"), EWebToUECssProperty::MaxHeight },
			{ TEXT("left"), EWebToUECssProperty::Left }, { TEXT("top"), EWebToUECssProperty::Top },
			{ TEXT("right"), EWebToUECssProperty::Right }, { TEXT("bottom"), EWebToUECssProperty::Bottom },
			{ TEXT("margin"), EWebToUECssProperty::Margin }, { TEXT("margin-left"), EWebToUECssProperty::MarginLeft },
			{ TEXT("margin-top"), EWebToUECssProperty::MarginTop }, { TEXT("margin-right"), EWebToUECssProperty::MarginRight },
			{ TEXT("margin-bottom"), EWebToUECssProperty::MarginBottom },
			{ TEXT("padding"), EWebToUECssProperty::Padding }, { TEXT("padding-left"), EWebToUECssProperty::PaddingLeft },
			{ TEXT("padding-top"), EWebToUECssProperty::PaddingTop }, { TEXT("padding-right"), EWebToUECssProperty::PaddingRight },
			{ TEXT("padding-bottom"), EWebToUECssProperty::PaddingBottom },
			{ TEXT("gap"), EWebToUECssProperty::Gap }, { TEXT("row-gap"), EWebToUECssProperty::RowGap },
			{ TEXT("column-gap"), EWebToUECssProperty::ColumnGap },
			{ TEXT("flex"), EWebToUECssProperty::Flex }, { TEXT("flex-direction"), EWebToUECssProperty::FlexDirection },
			{ TEXT("flex-wrap"), EWebToUECssProperty::FlexWrap }, { TEXT("flex-grow"), EWebToUECssProperty::FlexGrow },
			{ TEXT("flex-shrink"), EWebToUECssProperty::FlexShrink }, { TEXT("flex-basis"), EWebToUECssProperty::FlexBasis },
			{ TEXT("justify-content"), EWebToUECssProperty::JustifyContent },
			{ TEXT("align-items"), EWebToUECssProperty::AlignItems }, { TEXT("align-self"), EWebToUECssProperty::AlignSelf },
			{ TEXT("color"), EWebToUECssProperty::Color }, { TEXT("background"), EWebToUECssProperty::Background },
			{ TEXT("background-color"), EWebToUECssProperty::BackgroundColor }, { TEXT("border"), EWebToUECssProperty::Border },
			{ TEXT("border-color"), EWebToUECssProperty::BorderColor }, { TEXT("border-width"), EWebToUECssProperty::BorderWidth },
			{ TEXT("border-style"), EWebToUECssProperty::BorderStyle }, { TEXT("border-radius"), EWebToUECssProperty::BorderRadius },
			{ TEXT("opacity"), EWebToUECssProperty::Opacity }, { TEXT("font-family"), EWebToUECssProperty::FontFamily },
			{ TEXT("font-size"), EWebToUECssProperty::FontSize }, { TEXT("font-weight"), EWebToUECssProperty::FontWeight },
			{ TEXT("text-align"), EWebToUECssProperty::TextAlign }, { TEXT("white-space"), EWebToUECssProperty::WhiteSpace },
			{ TEXT("object-fit"), EWebToUECssProperty::ObjectFit }, { TEXT("z-index"), EWebToUECssProperty::ZIndex }
		};
		const EWebToUECssProperty* Found = Properties.Find(Name.TrimStartAndEnd().ToLower());
		if (!Found) return false;
		OutProperty = *Found;
		return true;
	}

	const TCHAR* LexToString(EWebToUECssProperty Property)
	{
		switch (Property)
		{
		case EWebToUECssProperty::Display: return TEXT("display"); case EWebToUECssProperty::Position: return TEXT("position");
		case EWebToUECssProperty::Visibility: return TEXT("visibility"); case EWebToUECssProperty::Overflow: return TEXT("overflow");
		case EWebToUECssProperty::Width: return TEXT("width"); case EWebToUECssProperty::Height: return TEXT("height");
		case EWebToUECssProperty::MinWidth: return TEXT("min-width"); case EWebToUECssProperty::MinHeight: return TEXT("min-height");
		case EWebToUECssProperty::MaxWidth: return TEXT("max-width"); case EWebToUECssProperty::MaxHeight: return TEXT("max-height");
		case EWebToUECssProperty::Left: return TEXT("left"); case EWebToUECssProperty::Top: return TEXT("top");
		case EWebToUECssProperty::Right: return TEXT("right"); case EWebToUECssProperty::Bottom: return TEXT("bottom");
		case EWebToUECssProperty::Margin: return TEXT("margin"); case EWebToUECssProperty::MarginLeft: return TEXT("margin-left");
		case EWebToUECssProperty::MarginTop: return TEXT("margin-top"); case EWebToUECssProperty::MarginRight: return TEXT("margin-right");
		case EWebToUECssProperty::MarginBottom: return TEXT("margin-bottom"); case EWebToUECssProperty::Padding: return TEXT("padding");
		case EWebToUECssProperty::PaddingLeft: return TEXT("padding-left"); case EWebToUECssProperty::PaddingTop: return TEXT("padding-top");
		case EWebToUECssProperty::PaddingRight: return TEXT("padding-right"); case EWebToUECssProperty::PaddingBottom: return TEXT("padding-bottom");
		case EWebToUECssProperty::Gap: return TEXT("gap"); case EWebToUECssProperty::RowGap: return TEXT("row-gap");
		case EWebToUECssProperty::ColumnGap: return TEXT("column-gap"); case EWebToUECssProperty::Flex: return TEXT("flex");
		case EWebToUECssProperty::FlexDirection: return TEXT("flex-direction"); case EWebToUECssProperty::FlexWrap: return TEXT("flex-wrap");
		case EWebToUECssProperty::FlexGrow: return TEXT("flex-grow"); case EWebToUECssProperty::FlexShrink: return TEXT("flex-shrink");
		case EWebToUECssProperty::FlexBasis: return TEXT("flex-basis"); case EWebToUECssProperty::JustifyContent: return TEXT("justify-content");
		case EWebToUECssProperty::AlignItems: return TEXT("align-items"); case EWebToUECssProperty::AlignSelf: return TEXT("align-self");
		case EWebToUECssProperty::Color: return TEXT("color"); case EWebToUECssProperty::Background: return TEXT("background");
		case EWebToUECssProperty::BackgroundColor: return TEXT("background-color"); case EWebToUECssProperty::Border: return TEXT("border");
		case EWebToUECssProperty::BorderColor: return TEXT("border-color"); case EWebToUECssProperty::BorderWidth: return TEXT("border-width");
		case EWebToUECssProperty::BorderStyle: return TEXT("border-style"); case EWebToUECssProperty::BorderRadius: return TEXT("border-radius");
		case EWebToUECssProperty::Opacity: return TEXT("opacity"); case EWebToUECssProperty::FontFamily: return TEXT("font-family");
		case EWebToUECssProperty::FontSize: return TEXT("font-size"); case EWebToUECssProperty::FontWeight: return TEXT("font-weight");
		case EWebToUECssProperty::TextAlign: return TEXT("text-align"); case EWebToUECssProperty::WhiteSpace: return TEXT("white-space");
		case EWebToUECssProperty::ObjectFit: return TEXT("object-fit"); case EWebToUECssProperty::ZIndex: return TEXT("z-index");
		default: return TEXT("invalid");
		}
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
		default: break;
		}
	}

	void ApplyProperties(const TMap<EWebToUECssProperty, FWebToUEStyleValue>& Properties,
		FWebToUEComputedStyle& Style)
	{
		for (uint8 RawProperty = static_cast<uint8>(EWebToUECssProperty::Display);
			RawProperty <= static_cast<uint8>(EWebToUECssProperty::ZIndex); ++RawProperty)
		{
			const EWebToUECssProperty Property = static_cast<EWebToUECssProperty>(RawProperty);
			if (const FWebToUEStyleValue* Value = Properties.Find(Property))
			{
				ApplyProperty(Property, *Value, Style);
			}
		}
	}
}
