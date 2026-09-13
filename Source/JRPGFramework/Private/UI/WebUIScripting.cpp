#include "UI/WebUIScripting.h"

FString JRPGWebUI::OptionalNameToJS(FName In)
{
	// NAME_None -> '' (falsy no JS), e não a palavra "None".
	return ToJSStringLiteral(In.IsNone() ? FString() : In.ToString());
}

FString JRPGWebUI::ToJSStringLiteral(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len() + 8);
	Out.AppendChar(TEXT('\''));

	for (const TCHAR C : In)
	{
		switch (C)
		{
		case TEXT('\\'): Out.Append(TEXT("\\\\")); break;
		case TEXT('\''): Out.Append(TEXT("\\'"));  break;
		case TEXT('"'):  Out.Append(TEXT("\\\"")); break;
		case TEXT('\n'): Out.Append(TEXT("\\n"));  break;
		case TEXT('\r'): Out.Append(TEXT("\\r"));  break;
		case TEXT('\t'): Out.Append(TEXT("\\t"));  break;
		default:
			// U+2028/U+2029 são quebras de linha para o parser JS (terminariam
			// o literal); controles < 0x20 nunca são válidos crus num literal
			if (C < 0x20 || C == 0x2028 || C == 0x2029)
			{
				Out.Append(FString::Printf(TEXT("\\u%04X"), static_cast<int32>(C)));
			}
			else
			{
				Out.AppendChar(C);
			}
			break;
		}
	}

	Out.AppendChar(TEXT('\''));
	return Out;
}
