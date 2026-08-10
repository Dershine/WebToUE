#include "WebToUECompiler.h"

#include "Algo/Sort.h"
#include "Internationalization/Regex.h"
#include <yoga/Yoga.h>

namespace WebToUE::Private
{
	static const TSet<FString> KnownTags = {
		TEXT("html"), TEXT("head"), TEXT("body"), TEXT("style"), TEXT("link"),
		TEXT("div"), TEXT("span"), TEXT("p"), TEXT("img"), TEXT("button")
	};

	static FString DecodeEntities(FString Value)
	{
		Value.ReplaceInline(TEXT("&lt;"), TEXT("<"), ESearchCase::IgnoreCase);
		Value.ReplaceInline(TEXT("&gt;"), TEXT(">"), ESearchCase::IgnoreCase);
		Value.ReplaceInline(TEXT("&quot;"), TEXT("\""), ESearchCase::IgnoreCase);
		Value.ReplaceInline(TEXT("&apos;"), TEXT("'"), ESearchCase::IgnoreCase);
		Value.ReplaceInline(TEXT("&amp;"), TEXT("&"), ESearchCase::IgnoreCase);

		const FRegexPattern NumericEntity(TEXT("&#(x?[0-9a-fA-F]+);"));
		FRegexMatcher Matcher(NumericEntity, Value);
		while (Matcher.FindNext())
		{
			const FString Digits = Matcher.GetCaptureGroup(1);
			const bool bHex = Digits.StartsWith(TEXT("x"), ESearchCase::IgnoreCase);
			const int32 Codepoint = FCString::Strtoi(*Digits.Mid(bHex ? 1 : 0), nullptr, bHex ? 16 : 10);
			FString Replacement;
			Replacement.AppendChar(static_cast<TCHAR>(Codepoint));
			Value = Value.Left(Matcher.GetMatchBeginning()) + Replacement + Value.Mid(Matcher.GetMatchEnding());
			Matcher = FRegexMatcher(NumericEntity, Value);
		}
		return Value;
	}

	class FHtmlParser
	{
	public:
		FHtmlParser(const FString& InSource, const FString& InSourceName, FWebToUEDocument& InDocument)
			: Source(InSource), SourceName(InSourceName), Document(InDocument)
		{
		}

		void Parse(FString& OutInlineCss)
		{
			TSharedPtr<FWebToUENode> SyntheticRoot = MakeShared<FWebToUENode>();
			SyntheticRoot->Tag = TEXT("body");
			Stack.Add(SyntheticRoot);

			while (!AtEnd())
			{
				if (StartsWith(TEXT("<!--")))
				{
					SkipUntil(TEXT("-->"));
				}
				else if (StartsWith(TEXT("<!")))
				{
					SkipUntil(TEXT(">"));
				}
				else if (Peek() == TEXT('<'))
				{
					ParseTag();
				}
				else
				{
					ParseText(OutInlineCss);
				}
			}

			TSharedPtr<FWebToUENode> Body;
			TFunction<void(const TSharedPtr<FWebToUENode>&)> FindBody = [&](const TSharedPtr<FWebToUENode>& Node)
			{
				if (!Node || Body)
				{
					return;
				}
				if (Node->Tag == TEXT("body"))
				{
					Body = Node;
					return;
				}
				for (const TSharedPtr<FWebToUENode>& Child : Node->Children)
				{
					FindBody(Child);
				}
			};
			for (const TSharedPtr<FWebToUENode>& Child : SyntheticRoot->Children)
			{
				FindBody(Child);
			}
			if (Body)
			{
				Body->Parent = nullptr;
				Document.Root = Body;
			}
			else
			{
				Document.Root = SyntheticRoot;
			}
		}

	private:
		const FString& Source;
		FString SourceName;
		FWebToUEDocument& Document;
		TArray<TSharedPtr<FWebToUENode>> Stack;
		int32 Pos = 0;
		int32 Line = 1;
		int32 Column = 1;

		bool AtEnd() const { return Pos >= Source.Len(); }
		TCHAR Peek(int32 Offset = 0) const { return Pos + Offset < Source.Len() ? Source[Pos + Offset] : 0; }
		bool StartsWith(const TCHAR* Text) const { return Source.Mid(Pos).StartsWith(Text, ESearchCase::IgnoreCase); }

		TCHAR Advance()
		{
			const TCHAR Result = Peek();
			++Pos;
			if (Result == TEXT('\n'))
			{
				++Line;
				Column = 1;
			}
			else
			{
				++Column;
			}
			return Result;
		}

		void SkipWhitespace()
		{
			while (!AtEnd() && FChar::IsWhitespace(Peek()))
			{
				Advance();
			}
		}

		void SkipUntil(const TCHAR* Terminator)
		{
			while (!AtEnd() && !StartsWith(Terminator))
			{
				Advance();
			}
			for (int32 Index = 0; !AtEnd() && Index < FCString::Strlen(Terminator); ++Index)
			{
				Advance();
			}
		}

		FString ParseName()
		{
			const int32 Start = Pos;
			while (!AtEnd() && (FChar::IsAlnum(Peek()) || Peek() == TEXT('-') || Peek() == TEXT('_') || Peek() == TEXT(':')))
			{
				Advance();
			}
			return Source.Mid(Start, Pos - Start).ToLower();
		}

		FString ParseAttributeValue()
		{
			SkipWhitespace();
			if (Peek() == TEXT('"') || Peek() == TEXT('\''))
			{
				const TCHAR Quote = Advance();
				const int32 Start = Pos;
				while (!AtEnd() && Peek() != Quote)
				{
					Advance();
				}
				const FString Value = Source.Mid(Start, Pos - Start);
				if (!AtEnd())
				{
					Advance();
				}
				return DecodeEntities(Value);
			}
			const int32 Start = Pos;
			while (!AtEnd() && !FChar::IsWhitespace(Peek()) && Peek() != TEXT('>'))
			{
				Advance();
			}
			return DecodeEntities(Source.Mid(Start, Pos - Start));
		}

		void ParseTag()
		{
			const int32 TagLine = Line;
			const int32 TagColumn = Column;
			Advance();
			if (Peek() == TEXT('/'))
			{
				Advance();
				SkipWhitespace();
				const FString ClosingTag = ParseName();
				SkipUntil(TEXT(">"));
				for (int32 Index = Stack.Num() - 1; Index > 0; --Index)
				{
					if (Stack[Index]->Tag == ClosingTag)
					{
						Stack.SetNum(Index);
						return;
					}
				}
				AddDiagnostic(EWebToUEDiagnosticSeverity::Warning, TagLine, TagColumn,
					FString::Printf(TEXT("Unmatched closing tag </%s>."), *ClosingTag));
				return;
			}

			SkipWhitespace();
			const FString Tag = ParseName();
			if (Tag.IsEmpty())
			{
				AddDiagnostic(EWebToUEDiagnosticSeverity::Error, TagLine, TagColumn, TEXT("Expected an HTML tag name."));
				SkipUntil(TEXT(">"));
				return;
			}

			TSharedPtr<FWebToUENode> Node = MakeShared<FWebToUENode>();
			Node->Tag = Tag;
			Node->Parent = Stack.Last().Get();
			bool bSelfClosing = Tag == TEXT("img") || Tag == TEXT("link");
			while (!AtEnd() && Peek() != TEXT('>'))
			{
				SkipWhitespace();
				if (Peek() == TEXT('/'))
				{
					bSelfClosing = true;
					Advance();
					continue;
				}
				if (Peek() == TEXT('>'))
				{
					break;
				}
				const FString Name = ParseName();
				if (Name.IsEmpty())
				{
					Advance();
					continue;
				}
				SkipWhitespace();
				FString Value = TEXT("true");
				if (Peek() == TEXT('='))
				{
					Advance();
					Value = ParseAttributeValue();
				}
				Node->Attributes.Add(Name, MoveTemp(Value));
			}
			if (!AtEnd())
			{
				Advance();
			}

			Stack.Last()->Children.Add(Node);
			if (!KnownTags.Contains(Tag))
			{
				AddDiagnostic(EWebToUEDiagnosticSeverity::Warning, TagLine, TagColumn,
					FString::Printf(TEXT("Unknown element <%s> is treated as a generic flex container."), *Tag));
			}
			if (Tag == TEXT("link") && Node->GetAttribute(TEXT("rel")).Equals(TEXT("stylesheet"), ESearchCase::IgnoreCase))
			{
				const FString Href = Node->GetAttribute(TEXT("href"));
				if (!Href.IsEmpty())
				{
					Document.LinkedStylesheets.AddUnique(Href);
				}
			}
			if (!bSelfClosing)
			{
				Stack.Add(Node);
			}
		}

		void ParseText(FString& OutInlineCss)
		{
			const int32 Start = Pos;
			while (!AtEnd() && Peek() != TEXT('<'))
			{
				Advance();
			}
			FString Text = Source.Mid(Start, Pos - Start);
			if (Stack.Last()->Tag == TEXT("style"))
			{
				OutInlineCss += Text + TEXT("\n");
				return;
			}
			Text = DecodeEntities(Text);
			Text.TrimStartAndEndInline();
			if (Text.IsEmpty() || Stack.Last()->Tag == TEXT("head"))
			{
				return;
			}
			TSharedPtr<FWebToUENode> TextNode = MakeShared<FWebToUENode>();
			TextNode->Type = EWebToUENodeType::Text;
			TextNode->Tag = TEXT("#text");
			TextNode->Text = MoveTemp(Text);
			TextNode->Parent = Stack.Last().Get();
			Stack.Last()->Children.Add(TextNode);
		}

		void AddDiagnostic(EWebToUEDiagnosticSeverity Severity, int32 InLine, int32 InColumn, FString Message)
		{
			Document.Diagnostics.Add({ Severity, SourceName, InLine, InColumn, MoveTemp(Message) });
		}
	};

	static void RemoveCssComments(FString& Css)
	{
		int32 SearchFrom = 0;
		while (SearchFrom < Css.Len())
		{
			const int32 Start = Css.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (Start == INDEX_NONE) break;
			if (Start + 1 < Css.Len() && Css[Start + 1] == TEXT('*'))
			{
				const int32 End = Css.Find(TEXT("*/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 2);
				Css.RemoveAt(Start, End == INDEX_NONE ? Css.Len() - Start : End + 2 - Start);
				SearchFrom = Start;
			}
			else
			{
				SearchFrom = Start + 1;
			}
		}
	}

	static bool IsSelectorNameChar(TCHAR Char)
	{
		return FChar::IsAlnum(Char) || Char == TEXT('-') || Char == TEXT('_');
	}

	static bool ParseSimpleSelector(const FString& Text, FWebToUESelectorSegment& Out, int32& Specificity)
	{
		int32 Pos = 0;
		if (Pos < Text.Len() && Text[Pos] == TEXT('*'))
		{
			++Pos;
		}
		else
		{
			const int32 Start = Pos;
			while (Pos < Text.Len() && IsSelectorNameChar(Text[Pos]))
			{
				++Pos;
			}
			if (Pos > Start)
			{
				Out.Type = Text.Mid(Start, Pos - Start).ToLower();
				Specificity += 1;
			}
		}

		while (Pos < Text.Len())
		{
			const TCHAR Prefix = Text[Pos++];
			const int32 Start = Pos;
			while (Pos < Text.Len() && IsSelectorNameChar(Text[Pos]))
			{
				++Pos;
			}
			const FString Name = Text.Mid(Start, Pos - Start).ToLower();
			if (Name.IsEmpty())
			{
				return false;
			}
			if (Prefix == TEXT('#'))
			{
				Out.Id = Name;
				Specificity += 100;
			}
			else if (Prefix == TEXT('.'))
			{
				Out.Classes.Add(Name);
				Specificity += 10;
			}
			else if (Prefix == TEXT(':'))
			{
				if (Name == TEXT("hover")) Out.RequiredState |= EWebToUEPseudoState::Hover;
				else if (Name == TEXT("active")) Out.RequiredState |= EWebToUEPseudoState::Active;
				else if (Name == TEXT("focus")) Out.RequiredState |= EWebToUEPseudoState::Focus;
				else if (Name == TEXT("disabled")) Out.RequiredState |= EWebToUEPseudoState::Disabled;
				else return false;
				Specificity += 10;
			}
			else
			{
				return false;
			}
		}
		return true;
	}

	static bool ParseSelector(const FString& SelectorText, FWebToUEStyleRule& Rule)
	{
		int32 Pos = 0;
		EWebToUECombinator PendingRelation = EWebToUECombinator::None;
		bool bHaveSegment = false;
		while (Pos < SelectorText.Len())
		{
			bool bSawWhitespace = false;
			while (Pos < SelectorText.Len() && FChar::IsWhitespace(SelectorText[Pos]))
			{
				bSawWhitespace = true;
				++Pos;
			}
			if (bSawWhitespace && bHaveSegment && PendingRelation == EWebToUECombinator::None)
			{
				PendingRelation = EWebToUECombinator::Descendant;
			}
			if (Pos < SelectorText.Len() && SelectorText[Pos] == TEXT('>'))
			{
				PendingRelation = EWebToUECombinator::Child;
				++Pos;
				continue;
			}
			if (Pos >= SelectorText.Len())
			{
				break;
			}
			const int32 Start = Pos;
			while (Pos < SelectorText.Len() && !FChar::IsWhitespace(SelectorText[Pos]) && SelectorText[Pos] != TEXT('>'))
			{
				++Pos;
			}
			FWebToUESelectorSegment Segment;
			Segment.RelationToPrevious = bHaveSegment ? PendingRelation : EWebToUECombinator::None;
			if (!ParseSimpleSelector(SelectorText.Mid(Start, Pos - Start), Segment, Rule.Specificity))
			{
				return false;
			}
			Rule.Selector.Add(MoveTemp(Segment));
			bHaveSegment = true;
			PendingRelation = EWebToUECombinator::None;
		}
		return Rule.Selector.Num() > 0;
	}

	static void ParseCss(const FString& InCss, const FString& SourceName, FWebToUEDocument& Document)
	{
		FString Css = InCss;
		RemoveCssComments(Css);
		int32 Pos = 0;
		int32 SourceOrder = Document.Rules.Num();
		while (Pos < Css.Len())
		{
			const int32 Open = Css.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (Open == INDEX_NONE)
			{
				break;
			}
			const int32 Close = Css.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open + 1);
			if (Close == INDEX_NONE)
			{
				Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Error, SourceName, 1, Open + 1, TEXT("Unterminated CSS rule block.") });
				break;
			}
			FString SelectorBlock = Css.Mid(Pos, Open - Pos).TrimStartAndEnd();
			const FString DeclarationBlock = Css.Mid(Open + 1, Close - Open - 1);
			Pos = Close + 1;
			if (SelectorBlock.StartsWith(TEXT("@")))
			{
				Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Warning, SourceName, 1, 1,
					FString::Printf(TEXT("Unsupported CSS at-rule '%s'."), *SelectorBlock) });
				continue;
			}

			TMap<FString, FString> Declarations;
			TArray<FString> DeclarationTexts;
			DeclarationBlock.ParseIntoArray(DeclarationTexts, TEXT(";"), true);
			for (FString Declaration : DeclarationTexts)
			{
				FString Name;
				FString Value;
				if (!Declaration.Split(TEXT(":"), &Name, &Value))
				{
					if (!Declaration.TrimStartAndEnd().IsEmpty())
					{
						Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Warning, SourceName, 1, 1,
							FString::Printf(TEXT("Ignored malformed CSS declaration '%s'."), *Declaration) });
					}
					continue;
				}
				Declarations.Add(Name.TrimStartAndEnd().ToLower(), Value.TrimStartAndEnd());
			}

			TArray<FString> Selectors;
			SelectorBlock.ParseIntoArray(Selectors, TEXT(","), true);
			for (FString Selector : Selectors)
			{
				FWebToUEStyleRule Rule;
				Rule.SourceOrder = SourceOrder++;
				Rule.Declarations = Declarations;
				if (ParseSelector(Selector.TrimStartAndEnd(), Rule))
				{
					Document.Rules.Add(MoveTemp(Rule));
				}
				else
				{
					Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Warning, SourceName, 1, 1,
						FString::Printf(TEXT("Ignored unsupported selector '%s'."), *Selector) });
				}
			}
		}
	}

	static FWebToUELength ParseLength(const FString& Raw)
	{
		FString Value = Raw.TrimStartAndEnd().ToLower();
		if (Value == TEXT("auto")) return FWebToUELength::Auto();
		if (Value.EndsWith(TEXT("%"))) return FWebToUELength::Percent(FCString::Atof(*Value.LeftChop(1)));
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
			if (Hex.Len() == 8)
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

	static void ApplyProperties(const TMap<FString, FString>& Properties, FWebToUEComputedStyle& Style)
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
				const FWebToUELength Length = ParseLength(Part);
				if (Length.Unit == EWebToUEUnit::Pixels) Style.BorderWidth = Length.Value;
				else ParseColor(Part, Style.BorderColor);
			}
		}
		if (const FString* V = Value(TEXT("background"))) ParseColor(*V, Style.BackgroundColor);
		if (const FString* V = Value(TEXT("display"))) Style.Display = V->Equals(TEXT("none"), ESearchCase::IgnoreCase) ? EWebToUEDisplay::None : EWebToUEDisplay::Flex;
		if (const FString* V = Value(TEXT("position"))) Style.Position = V->Equals(TEXT("absolute"), ESearchCase::IgnoreCase) ? EWebToUEPosition::Absolute : EWebToUEPosition::Relative;
		if (const FString* V = Value(TEXT("overflow"))) Style.Overflow = V->Equals(TEXT("hidden"), ESearchCase::IgnoreCase) ? EWebToUEOverflow::Hidden : EWebToUEOverflow::Visible;
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
		if (const FString* V = Value(TEXT("object-fit"))) Style.ObjectFit = V->ToLower();
		if (const FString* V = Value(TEXT("z-index"))) Style.ZIndex = FCString::Atoi(**V);
	}

	static bool SegmentMatches(const FWebToUESelectorSegment& Segment, const FWebToUENode& Node)
	{
		if (Node.Type != EWebToUENodeType::Element) return false;
		if (!Segment.Type.IsEmpty() && Node.Tag != Segment.Type) return false;
		if (!Segment.Id.IsEmpty() && !Node.GetAttribute(TEXT("id")).Equals(Segment.Id, ESearchCase::IgnoreCase)) return false;
		for (const FString& Class : Segment.Classes)
		{
			if (!Node.HasClass(Class)) return false;
		}
		return EnumHasAllFlags(Node.StateFlags, Segment.RequiredState);
	}

	static void ResolveNode(FWebToUENode& Node, const FWebToUEDocument& Document, const FWebToUEComputedStyle* ParentStyle)
	{
		FWebToUEComputedStyle Style;
		if (Node.Type == EWebToUENodeType::Text)
		{
			Style.FlexDirection = EWebToUEFlexDirection::Row;
		}
		if (Node.Tag == TEXT("body"))
		{
			Style.Width = FWebToUELength::Percent(100.0f);
			Style.Height = FWebToUELength::Percent(100.0f);
		}
		if (Node.Tag == TEXT("button"))
		{
			Style.FlexDirection = EWebToUEFlexDirection::Row;
			Style.JustifyContent = TEXT("center");
			Style.AlignItems = TEXT("center");
		}
		if (ParentStyle)
		{
			Style.Color = ParentStyle->Color;
			Style.FontFamily = ParentStyle->FontFamily;
			Style.FontSize = ParentStyle->FontSize;
			Style.FontWeight = ParentStyle->FontWeight;
			Style.TextAlign = ParentStyle->TextAlign;
		}

		TArray<const FWebToUEStyleRule*> Matches;
		for (const FWebToUEStyleRule& Rule : Document.Rules)
		{
			if (FWebToUEStyleResolver::Matches(Rule, Node)) Matches.Add(&Rule);
		}
		Matches.Sort([](const FWebToUEStyleRule& A, const FWebToUEStyleRule& B)
		{
			return A.Specificity == B.Specificity ? A.SourceOrder < B.SourceOrder : A.Specificity < B.Specificity;
		});
		TMap<FString, FString> Properties;
		for (const FWebToUEStyleRule* Rule : Matches)
		{
			for (const TPair<FString, FString>& Pair : Rule->Declarations) Properties.Add(Pair.Key, Pair.Value);
		}
		const FString InlineStyle = Node.GetAttribute(TEXT("style"));
		if (!InlineStyle.IsEmpty())
		{
			TArray<FString> Parts;
			InlineStyle.ParseIntoArray(Parts, TEXT(";"), true);
			for (FString Part : Parts)
			{
				FString Name, Value;
				if (Part.Split(TEXT(":"), &Name, &Value)) Properties.Add(Name.TrimStartAndEnd().ToLower(), Value.TrimStartAndEnd());
			}
		}
		ApplyProperties(Properties, Style);
		Style.bVisible = Style.bVisible && Node.bRuntimeVisible;
		Style.bEnabled = !Node.Attributes.Contains(TEXT("disabled")) && Node.bRuntimeEnabled;
		if (!Style.bEnabled) Node.StateFlags |= EWebToUEPseudoState::Disabled;
		else Node.StateFlags &= ~EWebToUEPseudoState::Disabled;
		Node.Style = MoveTemp(Style);
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			ResolveNode(*Child, Document, &Node.Style);
		}
	}

	static void SetDimension(YGNodeRef Node, const FWebToUELength& Length, void (*SetPoint)(YGNodeRef, float), void (*SetPercent)(YGNodeRef, float), void (*SetAuto)(YGNodeRef))
	{
		switch (Length.Unit)
		{
		case EWebToUEUnit::Pixels: SetPoint(Node, Length.Value); break;
		case EWebToUEUnit::Percent: SetPercent(Node, Length.Value); break;
		case EWebToUEUnit::Auto: if (SetAuto) SetAuto(Node); break;
		default: break;
		}
	}

	static void SetEdge(YGNodeRef Node, YGEdge Edge, const FWebToUELength& Length,
		void (*SetPoint)(YGNodeRef, YGEdge, float), void (*SetPercent)(YGNodeRef, YGEdge, float), void (*SetAuto)(YGNodeRef, YGEdge))
	{
		switch (Length.Unit)
		{
		case EWebToUEUnit::Pixels: SetPoint(Node, Edge, Length.Value); break;
		case EWebToUEUnit::Percent: SetPercent(Node, Edge, Length.Value); break;
		case EWebToUEUnit::Auto: if (SetAuto) SetAuto(Node, Edge); break;
		default: break;
		}
	}

	static YGAlign ToAlign(const FString& Value, YGAlign Fallback)
	{
		if (Value == TEXT("flex-start")) return YGAlignFlexStart;
		if (Value == TEXT("center")) return YGAlignCenter;
		if (Value == TEXT("flex-end")) return YGAlignFlexEnd;
		if (Value == TEXT("stretch")) return YGAlignStretch;
		if (Value == TEXT("baseline")) return YGAlignBaseline;
		if (Value == TEXT("auto")) return YGAlignAuto;
		return Fallback;
	}

	static YGJustify ToJustify(const FString& Value)
	{
		if (Value == TEXT("center")) return YGJustifyCenter;
		if (Value == TEXT("flex-end")) return YGJustifyFlexEnd;
		if (Value == TEXT("space-between")) return YGJustifySpaceBetween;
		if (Value == TEXT("space-around")) return YGJustifySpaceAround;
		if (Value == TEXT("space-evenly")) return YGJustifySpaceEvenly;
		return YGJustifyFlexStart;
	}

	static YGNodeRef BuildYogaTree(FWebToUENode& WebNode, const FWebToUELayoutEngine::FMeasureNode& MeasureNode)
	{
		YGNodeRef Node = YGNodeNew();
		const FWebToUEComputedStyle& S = WebNode.Style;
		YGNodeStyleSetDisplay(Node, S.Display == EWebToUEDisplay::None ? YGDisplayNone : YGDisplayFlex);
		YGNodeStyleSetPositionType(Node, S.Position == EWebToUEPosition::Absolute ? YGPositionTypeAbsolute : YGPositionTypeRelative);
		YGNodeStyleSetOverflow(Node, S.Overflow == EWebToUEOverflow::Hidden ? YGOverflowHidden : YGOverflowVisible);
		YGNodeStyleSetFlexDirection(Node,
			S.FlexDirection == EWebToUEFlexDirection::Row ? YGFlexDirectionRow :
			S.FlexDirection == EWebToUEFlexDirection::RowReverse ? YGFlexDirectionRowReverse :
			S.FlexDirection == EWebToUEFlexDirection::ColumnReverse ? YGFlexDirectionColumnReverse : YGFlexDirectionColumn);
		YGNodeStyleSetFlexWrap(Node, S.FlexWrap == TEXT("wrap") ? YGWrapWrap : S.FlexWrap == TEXT("wrap-reverse") ? YGWrapWrapReverse : YGWrapNoWrap);
		YGNodeStyleSetJustifyContent(Node, ToJustify(S.JustifyContent));
		YGNodeStyleSetAlignItems(Node, ToAlign(S.AlignItems, YGAlignStretch));
		YGNodeStyleSetAlignSelf(Node, ToAlign(S.AlignSelf, YGAlignAuto));
		YGNodeStyleSetFlexGrow(Node, S.FlexGrow);
		YGNodeStyleSetFlexShrink(Node, S.FlexShrink);
		SetDimension(Node, S.FlexBasis, YGNodeStyleSetFlexBasis, YGNodeStyleSetFlexBasisPercent, YGNodeStyleSetFlexBasisAuto);
		SetDimension(Node, S.Width, YGNodeStyleSetWidth, YGNodeStyleSetWidthPercent, YGNodeStyleSetWidthAuto);
		SetDimension(Node, S.Height, YGNodeStyleSetHeight, YGNodeStyleSetHeightPercent, YGNodeStyleSetHeightAuto);
		SetDimension(Node, S.MinWidth, YGNodeStyleSetMinWidth, YGNodeStyleSetMinWidthPercent, nullptr);
		SetDimension(Node, S.MinHeight, YGNodeStyleSetMinHeight, YGNodeStyleSetMinHeightPercent, nullptr);
		SetDimension(Node, S.MaxWidth, YGNodeStyleSetMaxWidth, YGNodeStyleSetMaxWidthPercent, nullptr);
		SetDimension(Node, S.MaxHeight, YGNodeStyleSetMaxHeight, YGNodeStyleSetMaxHeightPercent, nullptr);
		SetEdge(Node, YGEdgeLeft, S.Margin.Left, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
		SetEdge(Node, YGEdgeTop, S.Margin.Top, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
		SetEdge(Node, YGEdgeRight, S.Margin.Right, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
		SetEdge(Node, YGEdgeBottom, S.Margin.Bottom, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
		SetEdge(Node, YGEdgeLeft, S.Padding.Left, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr);
		SetEdge(Node, YGEdgeTop, S.Padding.Top, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr);
		SetEdge(Node, YGEdgeRight, S.Padding.Right, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr);
		SetEdge(Node, YGEdgeBottom, S.Padding.Bottom, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr);
		SetEdge(Node, YGEdgeLeft, S.Inset.Left, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr);
		SetEdge(Node, YGEdgeTop, S.Inset.Top, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr);
		SetEdge(Node, YGEdgeRight, S.Inset.Right, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr);
		SetEdge(Node, YGEdgeBottom, S.Inset.Bottom, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr);
		YGNodeStyleSetGap(Node, YGGutterRow, S.RowGap);
		YGNodeStyleSetGap(Node, YGGutterColumn, S.ColumnGap);
		YGNodeStyleSetBorder(Node, YGEdgeAll, S.BorderWidth);

		if ((WebNode.Type == EWebToUENodeType::Text || WebNode.Tag == TEXT("img")) && (!S.Width.IsDefined() || !S.Height.IsDefined()))
		{
			const FVector2f Measured = MeasureNode(WebNode);
			if (!S.Width.IsDefined()) YGNodeStyleSetWidth(Node, Measured.X);
			if (!S.Height.IsDefined()) YGNodeStyleSetHeight(Node, Measured.Y);
		}
		for (int32 Index = 0; Index < WebNode.Children.Num(); ++Index)
		{
			YGNodeInsertChild(Node, BuildYogaTree(*WebNode.Children[Index], MeasureNode), Index);
		}
		return Node;
	}

	static void CopyYogaLayout(FWebToUENode& WebNode, YGNodeConstRef Node, const FVector2f ParentPosition, int32& PaintOrder)
	{
		WebNode.Position = ParentPosition + FVector2f(YGNodeLayoutGetLeft(Node), YGNodeLayoutGetTop(Node));
		WebNode.Size = FVector2f(YGNodeLayoutGetWidth(Node), YGNodeLayoutGetHeight(Node));
		WebNode.PaintOrder = PaintOrder++;
		for (int32 Index = 0; Index < WebNode.Children.Num(); ++Index)
		{
			CopyYogaLayout(*WebNode.Children[Index], YGNodeGetChild(const_cast<YGNodeRef>(Node), Index), WebNode.Position, PaintOrder);
		}
	}
}

TSharedRef<FWebToUEDocument> FWebToUECompiler::Compile(const FString& Html, const FString& ExternalCss, const FString& SourceName)
{
	using namespace WebToUE::Private;
	TSharedRef<FWebToUEDocument> Document = MakeShared<FWebToUEDocument>();
	FString InlineCss;
	FHtmlParser(Html, SourceName, *Document).Parse(InlineCss);
	ParseCss(ExternalCss + TEXT("\n") + InlineCss, SourceName, *Document);
	FWebToUEStyleResolver::Resolve(*Document);
	return Document;
}

bool FWebToUEStyleResolver::Matches(const FWebToUEStyleRule& Rule, const FWebToUENode& Node)
{
	using namespace WebToUE::Private;
	if (Rule.Selector.IsEmpty()) return false;
	TFunction<bool(int32, const FWebToUENode*)> MatchAt = [&](int32 Index, const FWebToUENode* Current)
	{
		if (!Current || Index < 0 || !SegmentMatches(Rule.Selector[Index], *Current)) return false;
		if (Index == 0) return true;
		if (Rule.Selector[Index].RelationToPrevious == EWebToUECombinator::Child)
		{
			return MatchAt(Index - 1, Current->Parent);
		}
		for (const FWebToUENode* Ancestor = Current->Parent; Ancestor; Ancestor = Ancestor->Parent)
		{
			if (MatchAt(Index - 1, Ancestor)) return true;
		}
		return false;
	};
	return MatchAt(Rule.Selector.Num() - 1, &Node);
}

void FWebToUEStyleResolver::Resolve(FWebToUEDocument& Document)
{
	if (Document.Root)
	{
		WebToUE::Private::ResolveNode(*Document.Root, Document, nullptr);
	}
}

void FWebToUELayoutEngine::Layout(FWebToUEDocument& Document, const FVector2f& ViewportSize, const FMeasureNode& MeasureNode)
{
	if (!Document.Root) return;
	YGNodeRef Root = WebToUE::Private::BuildYogaTree(*Document.Root, MeasureNode);
	YGNodeStyleSetWidth(Root, ViewportSize.X);
	YGNodeStyleSetHeight(Root, ViewportSize.Y);
	YGNodeCalculateLayout(Root, ViewportSize.X, ViewportSize.Y, YGDirectionLTR);
	int32 PaintOrder = 0;
	WebToUE::Private::CopyYogaLayout(*Document.Root, Root, FVector2f::ZeroVector, PaintOrder);
	YGNodeFreeRecursive(Root);
}
