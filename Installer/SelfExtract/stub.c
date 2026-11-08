/*
 * Self-extracting installer stub for AI Blueprint Generator.
 *
 * Layout of the finished exe:   [this stub][payload.zip][uint64 payload size, little endian]["ABGPAYLD"]
 * The payload is the repository (Installer\ + AIBlueprintGen\ + LICENSE + README.md). At start the stub writes the
 * zip to %TEMP%, lets PowerShell unpack it and starts Installer\Install-AIBlueprintGen.ps1 (the installer window),
 * then deletes the temporary files. No console window is shown.
 *
 * Build: python Installer\build_exe.py   (needs:  pip install ziglang)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#define MAGIC "ABGPAYLD"
#define TITLE L"AI Blueprint Generator"

static void Fail(const wchar_t* Text)
{
	MessageBoxW(NULL, Text, TITLE, MB_OK | MB_ICONERROR);
}

static int ReadAt(HANDLE File, uint64_t Offset, void* Buffer, DWORD Size)
{
	LARGE_INTEGER Pos;
	Pos.QuadPart = (LONGLONG)Offset;
	if (!SetFilePointerEx(File, Pos, NULL, FILE_BEGIN)) return 0;
	DWORD Read = 0;
	return ReadFile(File, Buffer, Size, &Read, NULL) && Read == Size;
}

int WINAPI WinMain(HINSTANCE Instance, HINSTANCE Previous, LPSTR CmdLine, int Show)
{
	(void)Instance; (void)Previous; (void)CmdLine; (void)Show;

	wchar_t Self[MAX_PATH * 2];
	if (!GetModuleFileNameW(NULL, Self, (DWORD)(sizeof(Self) / sizeof(Self[0]))))
	{
		Fail(L"Не удалось определить путь к установщику.");
		return 1;
	}

	HANDLE In = CreateFileW(Self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (In == INVALID_HANDLE_VALUE)
	{
		Fail(L"Не удалось открыть файл установщика.");
		return 1;
	}
	LARGE_INTEGER Total;
	unsigned char Trailer[16];
	if (!GetFileSizeEx(In, &Total) || Total.QuadPart < 16 || !ReadAt(In, (uint64_t)Total.QuadPart - 16, Trailer, 16)
		|| memcmp(Trailer + 8, MAGIC, 8) != 0)
	{
		CloseHandle(In);
		Fail(L"В установщике нет вложенных файлов (повреждён или собран не полностью).");
		return 1;
	}
	uint64_t PayloadSize = 0;
	memcpy(&PayloadSize, Trailer, 8);
	if (PayloadSize == 0 || PayloadSize > (uint64_t)Total.QuadPart - 16)
	{
		CloseHandle(In);
		Fail(L"Повреждённые данные в установщике.");
		return 1;
	}
	const uint64_t PayloadStart = (uint64_t)Total.QuadPart - 16 - PayloadSize;

	// %TEMP%\AIBlueprintGen_setup_<pid>
	wchar_t Dir[MAX_PATH];
	DWORD Len = GetTempPathW(MAX_PATH, Dir);
	if (Len == 0 || Len > MAX_PATH - 64)
	{
		CloseHandle(In);
		Fail(L"Не удалось найти временную папку.");
		return 1;
	}
	wchar_t Leaf[64];
	swprintf(Leaf, 64, L"AIBlueprintGen_setup_%lu", (unsigned long)GetCurrentProcessId());
	wcscat(Dir, Leaf);
	CreateDirectoryW(Dir, NULL);

	wchar_t ZipPath[MAX_PATH + 32];
	wcscpy(ZipPath, Dir);
	wcscat(ZipPath, L"\\payload.zip");
	HANDLE Out = CreateFileW(ZipPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (Out == INVALID_HANDLE_VALUE)
	{
		CloseHandle(In);
		Fail(L"Не удалось записать временные файлы.");
		return 1;
	}
	static unsigned char Chunk[1 << 16];
	uint64_t Left = PayloadSize, Offset = PayloadStart;
	int Ok = 1;
	while (Left > 0 && Ok)
	{
		DWORD Part = Left > sizeof(Chunk) ? (DWORD)sizeof(Chunk) : (DWORD)Left;
		DWORD Written = 0;
		Ok = ReadAt(In, Offset, Chunk, Part) && WriteFile(Out, Chunk, Part, &Written, NULL) && Written == Part;
		Offset += Part;
		Left -= Part;
	}
	CloseHandle(In);
	CloseHandle(Out);
	if (!Ok)
	{
		Fail(L"Не удалось распаковать установщик (нет места на диске?).");
		return 1;
	}

	// The paths go through environment variables: no quoting problems with spaces or apostrophes.
	SetEnvironmentVariableW(L"ABG_SETUP_DIR", Dir);
	static const wchar_t Command[] =
		L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \""
		L"try { "
		L"$d = $env:ABG_SETUP_DIR; "
		L"Expand-Archive -LiteralPath (Join-Path $d 'payload.zip') -DestinationPath (Join-Path $d 'pkg') -Force; "
		L"& (Join-Path $d 'pkg\\Installer\\Install-AIBlueprintGen.ps1') "
		L"} catch { "
		L"Add-Type -AssemblyName System.Windows.Forms; "
		L"[System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'AI Blueprint Generator') | Out-Null "
		L"} finally { "
		L"Set-Location $env:TEMP; Remove-Item -LiteralPath $env:ABG_SETUP_DIR -Recurse -Force -ErrorAction SilentlyContinue "
		L"}\"";

	wchar_t CommandLine[sizeof(Command) / sizeof(Command[0]) + 8];
	wcscpy(CommandLine, Command);

	STARTUPINFOW Startup;
	PROCESS_INFORMATION Process;
	memset(&Startup, 0, sizeof(Startup));
	memset(&Process, 0, sizeof(Process));
	Startup.cb = sizeof(Startup);
	if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process))
	{
		Fail(L"Не удалось запустить Windows PowerShell (он нужен установщику).");
		return 1;
	}
	WaitForSingleObject(Process.hProcess, INFINITE);
	CloseHandle(Process.hThread);
	CloseHandle(Process.hProcess);
	return 0;
}
