#include "ABGSettings.h"

const UABGSettings* UABGSettings::Get()
{
	return GetDefault<UABGSettings>();
}

UABGSettings* UABGSettings::GetMutable()
{
	return GetMutableDefault<UABGSettings>();
}
