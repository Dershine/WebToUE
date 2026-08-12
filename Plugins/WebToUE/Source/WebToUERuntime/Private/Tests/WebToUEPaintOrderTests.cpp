#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"

#include <initializer_list>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPaintOrderCacheTest, "WebToUE.Runtime.PaintOrderCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEPaintOrderCacheTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='root'><button id='low'></button><button id='same-a'></button>"
			"<button id='same-b'></button><button id='high'></button><button id='hover'></button></div></body>"),
		TEXT("#low { z-index: -1; } #same-a, #same-b { z-index: 0; } #high { z-index: 2; } "
			"#hover { z-index: -2; } #hover:hover { z-index: 3; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);

	FWebToUENode* Root = nullptr;
	FWebToUENode* Hover = nullptr;
	Document->ForEachNode([&Root, &Hover](FWebToUENode& Node)
	{
		const FString Id = Node.GetAttribute(TEXT("id"));
		if (Id == TEXT("root")) Root = &Node;
		if (Id == TEXT("hover")) Hover = &Node;
	});
	TestNotNull(TEXT("The paint-order parent exists"), Root);
	TestNotNull(TEXT("The hover target exists"), Hover);
	if (!Root || !Hover)
	{
		return false;
	}

	auto TestOrder = [this, &View, Root](const TCHAR* Context, std::initializer_list<const TCHAR*> Expected)
	{
		const TConstArrayView<FWebToUENode*> Order = View->GetPaintOrderForTesting(*Root);
		TestEqual(*FString::Printf(TEXT("%s has the expected child count"), Context),
			Order.Num(), static_cast<int32>(Expected.size()));
		int32 Index = 0;
		for (const TCHAR* ExpectedId : Expected)
		{
			if (!Order.IsValidIndex(Index))
			{
				break;
			}
			TestEqual(*FString::Printf(TEXT("%s child %d preserves paint order"), Context, Index),
				Order[Index]->GetAttribute(TEXT("id")), FString(ExpectedId));
			++Index;
		}
	};

	TestOrder(TEXT("Initial cache"), { TEXT("hover"), TEXT("low"), TEXT("same-a"), TEXT("same-b"), TEXT("high") });
	View->SetHoveredNodeForTesting(Hover);
	TestOrder(TEXT("Hover-rebuilt cache"), { TEXT("low"), TEXT("same-a"), TEXT("same-b"), TEXT("high"), TEXT("hover") });
	return true;
}

#endif
