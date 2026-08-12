#include "WebToUECompiler.h"
#include "WebToUEStyleProperties.h"

#include "Internationalization/Regex.h"

namespace WebToUE::Private
{
	static const TSet<FString> KnownTags = {
		TEXT("html"), TEXT("head"), TEXT("body"), TEXT("style"), TEXT("link"),
		TEXT("div"), TEXT("span"), TEXT("p"), TEXT("img"), TEXT("button"),
		TEXT("strong"), TEXT("b"), TEXT("em"), TEXT("i"), TEXT("u"), TEXT("br")
	};

	static void ParseDeclarationBlock(const FString& Block, const FString& SourceName,
		int32 StartLine, int32 StartColumn, FWebToUEDocument& Document,
		TArray<FWebToUEStyleDeclaration>& OutDeclarations);

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

		void Parse(TArray<FWebToUEStyleSheetSource>& OutInlineStyleSheets)
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
					ParseText(OutInlineStyleSheets);
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

		FString ParseAttributeValue(int32& OutLine, int32& OutColumn)
		{
			SkipWhitespace();
			if (Peek() == TEXT('"') || Peek() == TEXT('\''))
			{
				const TCHAR Quote = Advance();
				OutLine = Line;
				OutColumn = Column;
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
			OutLine = Line;
			OutColumn = Column;
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
			bool bSelfClosing = Tag == TEXT("img") || Tag == TEXT("link") || Tag == TEXT("br");
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
				const int32 AttributeLine = Line;
				const int32 AttributeColumn = Column;
				const FString Name = ParseName();
				if (Name.IsEmpty())
				{
					Advance();
					continue;
				}
				SkipWhitespace();
				FString Value = TEXT("true");
				int32 ValueLine = AttributeLine;
				int32 ValueColumn = AttributeColumn;
				if (Peek() == TEXT('='))
				{
					Advance();
					Value = ParseAttributeValue(ValueLine, ValueColumn);
				}
				if (Name == TEXT("style"))
				{
					TArray<FWebToUEStyleDeclaration> ValidatedDeclarations;
					ParseDeclarationBlock(Value, SourceName, ValueLine, ValueColumn, Document, ValidatedDeclarations);
					Value.Reset();
					for (const FWebToUEStyleDeclaration& Declaration : ValidatedDeclarations)
					{
						if (!Value.IsEmpty()) Value += TEXT("; ");
						Value += Declaration.Name + TEXT(": ") + Declaration.Value;
					}
				}
				Node->Attributes.Add(Name, MoveTemp(Value));
			}
			if (!AtEnd())
			{
				Advance();
			}
			const bool bHasStringTable = !Node->GetAttribute(TEXT("data-ue-string-table")).IsEmpty();
			const bool bHasStringKey = !Node->GetAttribute(TEXT("data-ue-string-key")).IsEmpty();
			if (bHasStringTable != bHasStringKey)
			{
				AddDiagnostic(EWebToUEDiagnosticSeverity::Error, TagLine, TagColumn,
					TEXT("data-ue-string-table and data-ue-string-key must be specified together."));
			}
			if (bHasStringTable && !Node->GetAttribute(TEXT("data-ue-loc-key")).IsEmpty())
			{
				AddDiagnostic(EWebToUEDiagnosticSeverity::Error, TagLine, TagColumn,
					TEXT("String Table text cannot also declare data-ue-loc-key."));
			}
			const FString RichTextValue = Node->GetAttribute(TEXT("data-ue-rich-text"));
			if (!RichTextValue.IsEmpty() && !RichTextValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) &&
				!RichTextValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				AddDiagnostic(EWebToUEDiagnosticSeverity::Error, TagLine, TagColumn,
					TEXT("data-ue-rich-text must be true or false."));
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

		void ParseText(TArray<FWebToUEStyleSheetSource>& OutInlineStyleSheets)
		{
			const int32 StartLine = Line;
			const int32 StartColumn = Column;
			const int32 Start = Pos;
			while (!AtEnd() && Peek() != TEXT('<'))
			{
				Advance();
			}
			FString Text = Source.Mid(Start, Pos - Start);
			if (Stack.Last()->Tag == TEXT("style"))
			{
				if (!Text.IsEmpty())
				{
					OutInlineStyleSheets.Add({ MoveTemp(Text), SourceName, StartLine, StartColumn });
				}
				return;
			}
			Text = DecodeEntities(Text);
			const bool bHadLeadingWhitespace = !Text.IsEmpty() && FChar::IsWhitespace(Text[0]);
			const bool bHadTrailingWhitespace = !Text.IsEmpty() && FChar::IsWhitespace(Text[Text.Len() - 1]);
			Text.TrimStartAndEndInline();
			if (Text.IsEmpty())
			{
				if (bHadLeadingWhitespace && !Stack.Last()->Children.IsEmpty())
				{
					TSharedPtr<FWebToUENode> LastNode = Stack.Last()->Children.Last();
					while (LastNode && LastNode->Type == EWebToUENodeType::Element && !LastNode->Children.IsEmpty())
					{
						LastNode = LastNode->Children.Last();
					}
					if (LastNode && LastNode->Type == EWebToUENodeType::Text) LastNode->bTextHadTrailingWhitespace = true;
				}
				return;
			}
			if (Stack.Last()->Tag == TEXT("head"))
			{
				return;
			}
			TSharedPtr<FWebToUENode> TextNode = MakeShared<FWebToUENode>();
			TextNode->Type = EWebToUENodeType::Text;
			TextNode->Tag = TEXT("#text");
			TextNode->Text = MoveTemp(Text);
			TextNode->bTextHadLeadingWhitespace = bHadLeadingWhitespace;
			TextNode->bTextHadTrailingWhitespace = bHadTrailingWhitespace;
			TextNode->Parent = Stack.Last().Get();
			Stack.Last()->Children.Add(TextNode);
		}

		void AddDiagnostic(EWebToUEDiagnosticSeverity Severity, int32 InLine, int32 InColumn, FString Message)
		{
			Document.Diagnostics.Add({ Severity, SourceName, InLine, InColumn, MoveTemp(Message) });
		}
	};

	static bool IsSupportedInlineTag(const FString& Tag)
	{
		return Tag == TEXT("strong") || Tag == TEXT("b") || Tag == TEXT("em") || Tag == TEXT("i") ||
			Tag == TEXT("u") || Tag == TEXT("br") || Tag == TEXT("span");
	}

	static bool HasOnlyInlineContent(const FWebToUENode& Node, bool& bOutHasFormatting)
	{
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			if (Child->Type == EWebToUENodeType::Text) continue;
			if (!IsSupportedInlineTag(Child->Tag)) return false;
			if (Child->Tag != TEXT("span")) bOutHasFormatting = true;
			if (Child->Tag == TEXT("br")) continue;
			if (!HasOnlyInlineContent(*Child, bOutHasFormatting)) return false;
		}
		return true;
	}

	static FString EscapeRichText(FString Text)
	{
		Text.ReplaceInline(TEXT("&"), TEXT("&amp;"));
		Text.ReplaceInline(TEXT("<"), TEXT("&lt;"));
		Text.ReplaceInline(TEXT(">"), TEXT("&gt;"));
		return Text;
	}

	struct FRichTextFlags
	{
		bool bBold = false;
		bool bItalic = false;
		bool bUnderline = false;
	};

	struct FRichTextBuildContext
	{
		FString Markup;
		bool bHasContent = false;
		bool bPendingWhitespace = false;
		bool bAfterHardBreak = false;
	};

	static FString RichTextStyleName(const FRichTextFlags& Flags)
	{
		if (Flags.bBold && Flags.bItalic && Flags.bUnderline) return TEXT("strong_em_underline");
		if (Flags.bBold && Flags.bItalic) return TEXT("strong_em");
		if (Flags.bBold && Flags.bUnderline) return TEXT("strong_underline");
		if (Flags.bItalic && Flags.bUnderline) return TEXT("em_underline");
		if (Flags.bBold) return TEXT("strong");
		if (Flags.bItalic) return TEXT("em");
		if (Flags.bUnderline) return TEXT("underline");
		return FString();
	}

	static void AppendRichText(const FWebToUENode& Node, FRichTextFlags Flags, FRichTextBuildContext& Context)
	{
		if (Node.Type == EWebToUENodeType::Text)
		{
			if (Node.bTextHadLeadingWhitespace && Context.bHasContent && !Context.bAfterHardBreak)
			{
				Context.bPendingWhitespace = true;
			}
			const FString EscapedText = EscapeRichText(Node.Text);
			if (EscapedText.IsEmpty()) return;
			if (Context.bPendingWhitespace)
			{
				Context.Markup += TEXT(" ");
				Context.bPendingWhitespace = false;
			}
			const FString StyleName = RichTextStyleName(Flags);
			Context.Markup += StyleName.IsEmpty() ? EscapedText : FString::Printf(TEXT("<%s>%s</>"), *StyleName, *EscapedText);
			Context.bHasContent = true;
			Context.bAfterHardBreak = false;
			Context.bPendingWhitespace = Node.bTextHadTrailingWhitespace;
			return;
		}
		if (Node.Tag == TEXT("br"))
		{
			Context.bPendingWhitespace = false;
			Context.Markup += TEXT("\n");
			Context.bHasContent = true;
			Context.bAfterHardBreak = true;
			return;
		}
		Flags.bBold |= Node.Tag == TEXT("strong") || Node.Tag == TEXT("b");
		Flags.bItalic |= Node.Tag == TEXT("em") || Node.Tag == TEXT("i");
		Flags.bUnderline |= Node.Tag == TEXT("u");
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children) AppendRichText(*Child, Flags, Context);
	}

	static void CollapseRichText(FWebToUENode& Node)
	{
		if (Node.Type != EWebToUENodeType::Element || Node.Children.IsEmpty()) return;

		bool bHasFormatting = false;
		const bool bInlineContent = HasOnlyInlineContent(Node, bHasFormatting);
		const bool bExplicitRichText = Node.GetAttribute(TEXT("data-ue-rich-text")).Equals(TEXT("true"), ESearchCase::IgnoreCase);
		if (bInlineContent && (bHasFormatting || bExplicitRichText))
		{
			FRichTextBuildContext Context;
			for (const TSharedPtr<FWebToUENode>& Child : Node.Children) AppendRichText(*Child, FRichTextFlags(), Context);
			TSharedPtr<FWebToUENode> TextNode = MakeShared<FWebToUENode>();
			TextNode->Type = EWebToUENodeType::Text;
			TextNode->Tag = TEXT("#text");
			TextNode->Text = MoveTemp(Context.Markup);
			TextNode->bRichText = true;
			TextNode->Parent = &Node;
			Node.Children.Reset();
			Node.Children.Add(MoveTemp(TextNode));
			return;
		}
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children) CollapseRichText(*Child);
	}

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
				const int32 ReplaceEnd = End == INDEX_NONE ? Css.Len() : End + 2;
				for (int32 Index = Start; Index < ReplaceEnd; ++Index)
				{
					if (Css[Index] != TEXT('\r') && Css[Index] != TEXT('\n')) Css[Index] = TEXT(' ');
				}
				SearchFrom = ReplaceEnd;
			}
			else
			{
				SearchFrom = Start + 1;
			}
		}
	}

	static void GetSourceLocation(const FString& Source, int32 Offset, int32 StartLine, int32 StartColumn,
		int32& OutLine, int32& OutColumn)
	{
		OutLine = StartLine;
		OutColumn = StartColumn;
		for (int32 Index = 0; Index < FMath::Min(Offset, Source.Len()); ++Index)
		{
			if (Source[Index] == TEXT('\n'))
			{
				++OutLine;
				OutColumn = 1;
			}
			else
			{
				++OutColumn;
			}
		}
	}

	static int32 SkipWhitespace(const FString& Source, int32 Offset, int32 End)
	{
		while (Offset < End && FChar::IsWhitespace(Source[Offset])) ++Offset;
		return Offset;
	}

	static int32 TrimWhitespaceEnd(const FString& Source, int32 Start, int32 End)
	{
		while (End > Start && FChar::IsWhitespace(Source[End - 1])) --End;
		return End;
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

	static void ParseCss(const FWebToUEStyleSheetSource& StyleSheet, FWebToUEDocument& Document)
	{
		FString Css = StyleSheet.Css;
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
				int32 Line;
				int32 Column;
				GetSourceLocation(Css, Open, StyleSheet.StartLine, StyleSheet.StartColumn, Line, Column);
				Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Error, StyleSheet.SourceName, Line, Column,
					TEXT("Unterminated CSS rule block.") });
				break;
			}
			const int32 SelectorStart = SkipWhitespace(Css, Pos, Open);
			const int32 SelectorEnd = TrimWhitespaceEnd(Css, SelectorStart, Open);
			const FString SelectorBlock = Css.Mid(SelectorStart, SelectorEnd - SelectorStart);
			Pos = Close + 1;
			if (SelectorBlock.StartsWith(TEXT("@")))
			{
				int32 Line;
				int32 Column;
				GetSourceLocation(Css, SelectorStart, StyleSheet.StartLine, StyleSheet.StartColumn, Line, Column);
				Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Warning, StyleSheet.SourceName, Line, Column,
					FString::Printf(TEXT("Unsupported CSS at-rule '%s'."), *SelectorBlock) });
				continue;
			}

			TArray<FWebToUEStyleDeclaration> Declarations;
			int32 DeclarationLine;
			int32 DeclarationColumn;
			GetSourceLocation(Css, Open + 1, StyleSheet.StartLine, StyleSheet.StartColumn, DeclarationLine, DeclarationColumn);
			ParseDeclarationBlock(Css.Mid(Open + 1, Close - Open - 1), StyleSheet.SourceName,
				DeclarationLine, DeclarationColumn, Document, Declarations);

			int32 GroupOffset = 0;
			while (GroupOffset <= SelectorBlock.Len())
			{
				const int32 Comma = SelectorBlock.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromStart, GroupOffset);
				const int32 RawEnd = Comma == INDEX_NONE ? SelectorBlock.Len() : Comma;
				const int32 LocalStart = SkipWhitespace(SelectorBlock, GroupOffset, RawEnd);
				const int32 LocalEnd = TrimWhitespaceEnd(SelectorBlock, LocalStart, RawEnd);
				const FString Selector = SelectorBlock.Mid(LocalStart, LocalEnd - LocalStart);
				if (Selector.IsEmpty())
				{
					if (Comma == INDEX_NONE) break;
					GroupOffset = Comma + 1;
					continue;
				}
				FWebToUEStyleRule Rule;
				Rule.SourceOrder = SourceOrder++;
				Rule.Declarations = Declarations;
				if (ParseSelector(Selector, Rule))
				{
					Document.Rules.Add(MoveTemp(Rule));
				}
				else
				{
					int32 Line;
					int32 Column;
					GetSourceLocation(Css, SelectorStart + LocalStart, StyleSheet.StartLine, StyleSheet.StartColumn, Line, Column);
					Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Warning, StyleSheet.SourceName, Line, Column,
						FString::Printf(TEXT("Ignored unsupported selector '%s'."), *Selector) });
				}
				if (Comma == INDEX_NONE) break;
				GroupOffset = Comma + 1;
			}
		}
	}

	static void ParseDeclarationBlock(const FString& Block, const FString& SourceName,
		int32 StartLine, int32 StartColumn, FWebToUEDocument& Document,
		TArray<FWebToUEStyleDeclaration>& OutDeclarations)
	{
		int32 Offset = 0;
		while (Offset <= Block.Len())
		{
			const int32 Semicolon = Block.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Offset);
			const int32 RawEnd = Semicolon == INDEX_NONE ? Block.Len() : Semicolon;
			const int32 DeclarationStart = SkipWhitespace(Block, Offset, RawEnd);
			const int32 DeclarationEnd = TrimWhitespaceEnd(Block, DeclarationStart, RawEnd);
			if (DeclarationStart < DeclarationEnd)
			{
				const int32 Colon = Block.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, DeclarationStart);
				int32 Line;
				int32 Column;
				GetSourceLocation(Block, DeclarationStart, StartLine, StartColumn, Line, Column);
				if (Colon == INDEX_NONE || Colon >= DeclarationEnd)
				{
					const FString Declaration = Block.Mid(DeclarationStart, DeclarationEnd - DeclarationStart);
					Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Warning, SourceName, Line, Column,
						FString::Printf(TEXT("Ignored malformed CSS declaration '%s'."), *Declaration) });
				}
				else
				{
					const int32 NameEnd = TrimWhitespaceEnd(Block, DeclarationStart, Colon);
					const int32 ValueStart = SkipWhitespace(Block, Colon + 1, DeclarationEnd);
					const FString Name = Block.Mid(DeclarationStart, NameEnd - DeclarationStart).ToLower();
					const FString Value = Block.Mid(ValueStart, DeclarationEnd - ValueStart);
					if (!IsKnownCssProperty(Name))
					{
						Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Warning, SourceName, Line, Column,
							FString::Printf(TEXT("Ignored unsupported CSS property '%s'."), *Name) });
					}
					else if (!IsValidCssValue(Name, Value))
					{
						Document.Diagnostics.Add({ EWebToUEDiagnosticSeverity::Warning, SourceName, Line, Column,
							FString::Printf(TEXT("Ignored invalid value '%s' for CSS property '%s'."), *Value, *Name) });
					}
					else
					{
						FWebToUEStyleDeclaration& Declaration = OutDeclarations.AddDefaulted_GetRef();
						Declaration.Name = Name;
						Declaration.Value = Value;
					}
				}
			}
			if (Semicolon == INDEX_NONE) break;
			Offset = Semicolon + 1;
		}
	}

}

TSharedRef<FWebToUEDocument> FWebToUECompiler::Compile(const FString& Html, const FString& ExternalCss, const FString& SourceName)
{
	TArray<FWebToUEStyleSheetSource> ExternalStyleSheets;
	if (!ExternalCss.IsEmpty())
	{
		ExternalStyleSheets.Add({ ExternalCss, SourceName, 1, 1 });
	}
	return Compile(Html, ExternalStyleSheets, SourceName);
}

TSharedRef<FWebToUEDocument> FWebToUECompiler::Compile(const FString& Html,
	TConstArrayView<FWebToUEStyleSheetSource> ExternalStyleSheets, const FString& SourceName)
{
	using namespace WebToUE::Private;
	TSharedRef<FWebToUEDocument> Document = MakeShared<FWebToUEDocument>();
	TArray<FWebToUEStyleSheetSource> InlineStyleSheets;
	FHtmlParser(Html, SourceName, *Document).Parse(InlineStyleSheets);
	if (Document->Root) CollapseRichText(*Document->Root);
	for (const FWebToUEStyleSheetSource& StyleSheet : ExternalStyleSheets)
	{
		ParseCss(StyleSheet, *Document);
	}
	for (const FWebToUEStyleSheetSource& StyleSheet : InlineStyleSheets)
	{
		ParseCss(StyleSheet, *Document);
	}
	Document->InitializeRuntimeData();
	FWebToUEStyleResolver::Resolve(*Document);
	return Document;
}
