#include "WebToUESettings.h"

#include "Styling/CoreStyle.h"

FSlateFontInfo UWebToUESettings::ResolveFont(const FString& Family, float Size, const FString& Weight) const
{
	const FWebToUEFontFamily* Match = FontFamilies.FindByPredicate([&Family](const FWebToUEFontFamily& Candidate)
	{
		return Candidate.CssFamily.ToString().Equals(Family, ESearchCase::IgnoreCase);
	});
	if (Match)
	{
		if (UObject* FontObject = Match->FontObject.LoadSynchronous())
		{
			return FSlateFontInfo(FontObject, FMath::RoundToInt(Size), Match->Typeface);
		}
	}
	const FName Typeface = Weight == TEXT("bold") || FCString::Atoi(*Weight) >= 600 ? TEXT("Bold") : TEXT("Regular");
	return FCoreStyle::GetDefaultFontStyle(Typeface, FMath::RoundToInt(Size));
}
