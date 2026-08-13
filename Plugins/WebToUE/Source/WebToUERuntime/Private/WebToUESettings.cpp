#include "WebToUESettings.h"

#include "Styling/CoreStyle.h"

FSoftObjectPath UWebToUESettings::FindFontObjectPath(const FString& Family) const
{
	const FWebToUEFontFamily* Match = FontFamilies.FindByPredicate(
		[&Family](const FWebToUEFontFamily& Candidate)
		{
			return Candidate.CssFamily.ToString().Equals(Family, ESearchCase::IgnoreCase);
		});
	return Match ? Match->FontObject.ToSoftObjectPath() : FSoftObjectPath();
}

FSlateFontInfo UWebToUESettings::ResolveFont(const FString& Family, float Size,
	const FString& Weight, UObject* ResolvedFontObject) const
{
	const FWebToUEFontFamily* Match = FontFamilies.FindByPredicate([&Family](const FWebToUEFontFamily& Candidate)
	{
		return Candidate.CssFamily.ToString().Equals(Family, ESearchCase::IgnoreCase);
	});
	if (Match && ResolvedFontObject)
	{
		return FSlateFontInfo(ResolvedFontObject, FMath::RoundToInt(Size), Match->Typeface);
	}
	const FName Typeface = Weight == TEXT("bold") || FCString::Atoi(*Weight) >= 600 ? TEXT("Bold") : TEXT("Regular");
	return FCoreStyle::GetDefaultFontStyle(Typeface, FMath::RoundToInt(Size));
}
