<#
 AI Blueprint Generator - installer for Unreal Engine (Windows).

 Without parameters it opens a window. Everything can also be done from the command line:

   Install.bat -Mode Project -Project "D:\Games\My\My.uproject"           (engine is taken from the project)
   Install.bat -Mode Engine  -Engine  "C:\Program Files\Epic Games\UE_5.8"
   Install.bat -Mode Project -Project "...\My.uproject" -NoBuild          (sources only, the project compiles them)
   Install.bat -Mode Project -Project "...\My.uproject" -Uninstall

 The plugin is built with the engine's own RunUAT BuildPlugin (needs Visual Studio with the C++ workload,
 the same one Unreal itself needs), then copied: into <Project>\Plugins or into <Engine>\Engine\Plugins\Marketplace.
#>
[CmdletBinding()]
param(
	[ValidateSet('', 'Project', 'Engine')][string]$Mode = '',
	[string]$Project = '',
	[string]$Engine = '',
	[switch]$NoBuild,
	[switch]$NoEnable,
	[switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$PluginName = 'AIBlueprintGen'

# ------------------------------------------------------------------------------------------ helpers
$ScriptDir = $PSScriptRoot
if ([string]::IsNullOrEmpty($ScriptDir)) {
	# compiled with ps2exe: the folder of the exe
	$ScriptDir = Split-Path -Parent ([System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName)
}

function Find-PluginSource {
	foreach ($candidate in @(
		(Join-Path $ScriptDir $PluginName),
		(Join-Path (Split-Path -Parent $ScriptDir) $PluginName),
		(Join-Path $ScriptDir "Plugin\$PluginName"))) {
		if (Test-Path -LiteralPath (Join-Path $candidate "$PluginName.uplugin")) { return (Resolve-Path -LiteralPath $candidate).Path }
	}
	return $null
}

function Read-Json([string]$Path) {
	return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json)
}

function Get-EngineVersion([string]$EngineRoot) {
	$file = Join-Path $EngineRoot 'Engine\Build\Build.version'
	if (-not (Test-Path -LiteralPath $file)) { return $null }
	$v = Read-Json $file
	return "{0}.{1}.{2}" -f $v.MajorVersion, $v.MinorVersion, $v.PatchVersion
}

function Test-EngineRoot([string]$EngineRoot) {
	return ($EngineRoot -and (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat')))
}

# Every Unreal Engine that can be found: launcher installs (registry + LauncherInstalled.dat) and custom builds.
function Get-InstalledEngines {
	$found = @{}
	function Add-Engine([string]$Root, [string]$Label) {
		if (-not (Test-EngineRoot $Root)) { return }
		$full = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')
		if ($found.ContainsKey($full.ToLowerInvariant())) { return }
		$ver = Get-EngineVersion $full
		$found[$full.ToLowerInvariant()] = [pscustomobject]@{
			Path = $full; Version = $ver; Label = ("{0}  [{1}]" -f $(if ($ver) { "UE $ver" } else { $Label }), $full)
		}
	}
	try {
		foreach ($key in (Get-ChildItem 'HKLM:\SOFTWARE\EpicGames\Unreal Engine' -ErrorAction SilentlyContinue)) {
			$dir = (Get-ItemProperty -LiteralPath $key.PSPath -ErrorAction SilentlyContinue).InstalledDirectory
			if ($dir) { Add-Engine $dir ("UE " + $key.PSChildName) }
		}
	} catch { }
	try {
		$dat = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
		if (Test-Path -LiteralPath $dat) {
			foreach ($item in (Read-Json $dat).InstallationList) {
				if ($item.AppName -like 'UE_*') { Add-Engine $item.InstallLocation $item.AppName }
			}
		}
	} catch { }
	try {
		$builds = Get-ItemProperty 'HKCU:\Software\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
		if ($builds) {
			foreach ($prop in $builds.PSObject.Properties) {
				if ($prop.Name -notlike 'PS*') { Add-Engine ([string]$prop.Value) ("Custom " + $prop.Name) }
			}
		}
	} catch { }
	return @($found.Values | Sort-Object { try { [version]($_.Version) } catch { [version]'0.0' } } -Descending)
}

# The engine a .uproject points to ("5.8", a GUID of a custom build, or empty).
function Get-ProjectEngine([string]$UProject) {
	if (-not (Test-Path -LiteralPath $UProject)) { return $null }
	$assoc = [string](Read-Json $UProject).EngineAssociation
	if (-not $assoc) { return $null }
	if ($assoc -match '^\d+\.\d+$') {
		$dir = (Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$assoc" -ErrorAction SilentlyContinue).InstalledDirectory
		if (Test-EngineRoot $dir) { return $dir.TrimEnd('\') }
		foreach ($e in (Get-InstalledEngines)) { if ($e.Version -like "$assoc.*") { return $e.Path } }
		return $null
	}
	$builds = Get-ItemProperty 'HKCU:\Software\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
	if ($builds -and $builds.PSObject.Properties[$assoc]) {
		$dir = [string]$builds.PSObject.Properties[$assoc].Value
		if (Test-EngineRoot $dir) { return $dir.TrimEnd('\') }
	}
	return $null
}

function Test-Admin {
	$id = [Security.Principal.WindowsIdentity]::GetCurrent()
	return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Runs PowerShell commands as administrator (one UAC prompt). Throws if refused or failed.
function Invoke-Elevated([string]$Commands) {
	$wrapped = "try { $Commands } catch { Write-Error `$_; exit 1 }"
	$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($wrapped))
	$p = Start-Process powershell.exe -Verb RunAs -WindowStyle Hidden -Wait -PassThru `
		-ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $encoded)
	if ($p.ExitCode -ne 0) { throw "The elevated step failed (exit code $($p.ExitCode))." }
}

function ConvertTo-SingleQuoted([string]$Text) { return "'" + $Text.Replace("'", "''") + "'" }

# Copies a folder (without Intermediate/Saved/Binaries of the source) replacing the target.
function Copy-PluginFolder([string]$From, [string]$To, [bool]$NeedsAdmin, [scriptblock]$Log) {
	$cmd = "if (Test-Path -LiteralPath $(ConvertTo-SingleQuoted $To)) { Remove-Item -LiteralPath $(ConvertTo-SingleQuoted $To) -Recurse -Force }; " +
		"New-Item -ItemType Directory -Force -Path $(ConvertTo-SingleQuoted $To) | Out-Null; " +
		"Get-ChildItem -LiteralPath $(ConvertTo-SingleQuoted $From) -Force | Where-Object { `$_.Name -notin @('Intermediate','Saved','.vs') } | " +
		"ForEach-Object { Copy-Item -LiteralPath `$_.FullName -Destination $(ConvertTo-SingleQuoted $To) -Recurse -Force }"
	if ($NeedsAdmin -and -not (Test-Admin)) {
		& $Log 'Нужны права администратора - сейчас появится запрос Windows (UAC).'
		Invoke-Elevated $cmd
	} else {
		Invoke-Expression $cmd
	}
}

function Remove-PluginFolder([string]$Target, [bool]$NeedsAdmin) {
	$cmd = "if (Test-Path -LiteralPath $(ConvertTo-SingleQuoted $Target)) { Remove-Item -LiteralPath $(ConvertTo-SingleQuoted $Target) -Recurse -Force }"
	if ($NeedsAdmin -and -not (Test-Admin)) { Invoke-Elevated $cmd } else { Invoke-Expression $cmd }
}

# Builds the plugin for the given engine into a temp folder and returns the folder of the built plugin.
function Build-Plugin([string]$Source, [string]$EngineRoot, [scriptblock]$Log, [scriptblock]$Pump) {
	$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
	$out = Join-Path ([IO.Path]::GetTempPath()) ("AIBlueprintGen_build_" + [guid]::NewGuid().ToString('N').Substring(0, 8))
	$logFile = Join-Path $out 'build.log'
	New-Item -ItemType Directory -Force -Path $out | Out-Null
	$package = Join-Path $out 'Package'
	$uplugin = Join-Path $Source "$PluginName.uplugin"
	$arguments = "/c `"`"$uat`" BuildPlugin -Plugin=`"$uplugin`" -Package=`"$package`" -TargetPlatforms=Win64 -Rocket > `"$logFile`" 2>&1`""
	& $Log "Сборка плагина движком $(Get-EngineVersion $EngineRoot). Это занимает 1-5 минут, окно не закрывайте..."
	$p = Start-Process -FilePath 'cmd.exe' -ArgumentList $arguments -WindowStyle Hidden -PassThru
	$shown = 0
	while (-not $p.HasExited) {
		Start-Sleep -Milliseconds 400
		& $Pump
		if (Test-Path -LiteralPath $logFile) {
			try {
				$lines = @(Get-Content -LiteralPath $logFile -ErrorAction Stop)
				if ($lines.Count -gt $shown) { $lines[$shown..($lines.Count - 1)] | ForEach-Object { & $Log ('  ' + $_) }; $shown = $lines.Count }
			} catch { }
		}
	}
	if (Test-Path -LiteralPath $logFile) {
		$lines = @(Get-Content -LiteralPath $logFile)
		if ($lines.Count -gt $shown) { $lines[$shown..($lines.Count - 1)] | ForEach-Object { & $Log ('  ' + $_) } }
	}
	if ($p.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $package "$PluginName.uplugin"))) {
		throw "Сборка не удалась (код $($p.ExitCode)). Полный лог: $logFile"
	}
	return $package
}

# Adds / removes the plugin entry in the .uproject (a backup .bak is made once).
function Set-ProjectPluginEnabled([string]$UProject, [bool]$Enable) {
	$backup = $UProject + '.bak'
	if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $UProject -Destination $backup }
	$json = Read-Json $UProject
	$list = @()
	if ($json.PSObject.Properties['Plugins']) { $list = @($json.Plugins | Where-Object { $_.Name -ne $PluginName }) }
	if ($Enable) { $list += [pscustomobject]@{ Name = $PluginName; Enabled = $true } }
	if ($json.PSObject.Properties['Plugins']) { $json.Plugins = $list } else { $json | Add-Member -NotePropertyName Plugins -NotePropertyValue $list }
	$text = $json | ConvertTo-Json -Depth 32
	[IO.File]::WriteAllText($UProject, $text, (New-Object Text.UTF8Encoding($false)))
}

# ------------------------------------------------------------------------------------------ the action
function Invoke-Install {
	param([string]$Mode, [string]$Project, [string]$EngineRoot, [bool]$Build, [bool]$Enable, [bool]$Remove, [scriptblock]$Log, [scriptblock]$Pump)

	$source = Find-PluginSource
	if (-not $Remove -and -not $source) { throw "Не найдена папка плагина ($PluginName) рядом с установщиком." }

	if ($Mode -eq 'Project') {
		if (-not $Project -or -not (Test-Path -LiteralPath $Project) -or $Project -notlike '*.uproject') { throw 'Укажите файл проекта (.uproject).' }
		$target = Join-Path (Split-Path -Parent $Project) "Plugins\$PluginName"
		if ($Remove) {
			Remove-PluginFolder $target $false
			Set-ProjectPluginEnabled $Project $false
			& $Log "Плагин удалён из проекта: $target"
			return
		}
		if ($Build -and -not (Test-EngineRoot $EngineRoot)) { throw 'Не удалось определить движок проекта. Выберите движок в списке.' }
		$from = $source
		if ($Build) { $from = Build-Plugin $source $EngineRoot $Log $Pump }
		& $Log "Копирование в $target"
		Copy-PluginFolder $from $target $false $Log
		if ($Enable) { Set-ProjectPluginEnabled $Project $true; & $Log 'Плагин включён в .uproject (резервная копия: .uproject.bak).' }
		& $Log ''
		& $Log ('ГОТОВО. Откройте проект.' + $(if (-not $Build) { ' Если проект спросит про пересборку модулей - согласитесь (нужен Visual Studio).' } else { '' }))
	}
	else {
		if (-not (Test-EngineRoot $EngineRoot)) { throw 'Выберите установленный движок.' }
		$target = Join-Path $EngineRoot "Engine\Plugins\Marketplace\$PluginName"
		if ($Remove) {
			Remove-PluginFolder $target $true
			& $Log "Плагин удалён из движка: $target"
			return
		}
		$from = Build-Plugin $source $EngineRoot $Log $Pump
		& $Log "Копирование в $target"
		Copy-PluginFolder $from $target $true $Log
		& $Log ''
		& $Log 'ГОТОВО. В каждом проекте включите плагин: Edit > Plugins > "AI Blueprint Generator" (и перезапустите редактор).'
	}
}

# ------------------------------------------------------------------------------------------ command line
if ($Mode -ne '') {
	$log = { param($t) Write-Host $t }
	$pump = { }
	$engineRoot = $Engine
	if ($Mode -eq 'Project' -and -not $engineRoot) { $engineRoot = Get-ProjectEngine $Project }
	if ($Mode -eq 'Engine' -and -not $engineRoot) {
		$all = Get-InstalledEngines
		if ($all.Count -gt 0) { $engineRoot = $all[0].Path; Write-Host "Выбран движок: $($all[0].Label)" }
	}
	try {
		Invoke-Install -Mode $Mode -Project $Project -EngineRoot $engineRoot -Build (-not $NoBuild) -Enable (-not $NoEnable) -Remove $Uninstall.IsPresent -Log $log -Pump $pump
		exit 0
	} catch {
		Write-Host ("ОШИБКА: " + $_.Exception.Message) -ForegroundColor Red
		exit 1
	}
}

# ------------------------------------------------------------------------------------------ window
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$engines = @(Get-InstalledEngines)
$sourcePath = Find-PluginSource
$pluginVersion = ''
if ($sourcePath) { try { $pluginVersion = (Read-Json (Join-Path $sourcePath "$PluginName.uplugin")).VersionName } catch { } }

$form = New-Object Windows.Forms.Form
$form.Text = "AI Blueprint Generator - установка $pluginVersion"
$form.StartPosition = 'CenterScreen'
$form.ClientSize = New-Object Drawing.Size(720, 560)
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.Font = New-Object Drawing.Font('Segoe UI', 9)

function New-Label($text, $x, $y, $w = 680) {
	$l = New-Object Windows.Forms.Label; $l.Text = $text; $l.Location = New-Object Drawing.Point($x, $y)
	$l.Size = New-Object Drawing.Size($w, 20); $form.Controls.Add($l); return $l
}

$hint = New-Label ($(if ($sourcePath) { "Плагин: $sourcePath" } else { 'ВНИМАНИЕ: папка плагина AIBlueprintGen не найдена рядом с установщиком!' })) 16 10
$hint.ForeColor = $(if ($sourcePath) { [Drawing.Color]::DimGray } else { [Drawing.Color]::Firebrick })

$rbProject = New-Object Windows.Forms.RadioButton
$rbProject.Text = 'Установить в проект (движок определяется по проекту)'
$rbProject.Location = New-Object Drawing.Point(16, 38); $rbProject.Size = New-Object Drawing.Size(680, 22); $rbProject.Checked = $true
$form.Controls.Add($rbProject)
$rbEngine = New-Object Windows.Forms.RadioButton
$rbEngine.Text = 'Установить в движок (будет доступен во всех проектах этого движка; нужны права администратора)'
$rbEngine.Location = New-Object Drawing.Point(16, 62); $rbEngine.Size = New-Object Drawing.Size(690, 22)
$form.Controls.Add($rbEngine)

$null = New-Label 'Проект (.uproject):' 16 96
$tbProject = New-Object Windows.Forms.TextBox
$tbProject.Location = New-Object Drawing.Point(16, 118); $tbProject.Size = New-Object Drawing.Size(580, 24)
$form.Controls.Add($tbProject)
$btnBrowse = New-Object Windows.Forms.Button
$btnBrowse.Text = 'Обзор...'; $btnBrowse.Location = New-Object Drawing.Point(606, 116); $btnBrowse.Size = New-Object Drawing.Size(98, 27)
$form.Controls.Add($btnBrowse)

$lblEngine = New-Label 'Движок:' 16 154
$cbEngine = New-Object Windows.Forms.ComboBox
$cbEngine.DropDownStyle = 'DropDownList'
$cbEngine.Location = New-Object Drawing.Point(16, 176); $cbEngine.Size = New-Object Drawing.Size(688, 24)
$form.Controls.Add($cbEngine)
foreach ($e in $engines) { [void]$cbEngine.Items.Add($e.Label) }
if ($engines.Count -gt 0) { $cbEngine.SelectedIndex = 0 }
else { [void]$cbEngine.Items.Add('(движки не найдены - установите Unreal Engine через Epic Games Launcher)'); $cbEngine.SelectedIndex = 0 }

$cbBuild = New-Object Windows.Forms.CheckBox
$cbBuild.Text = 'Собрать плагин заранее (рекомендуется: проекту не потребуется компилировать C++)'
$cbBuild.Location = New-Object Drawing.Point(16, 212); $cbBuild.Size = New-Object Drawing.Size(690, 22); $cbBuild.Checked = $true
$form.Controls.Add($cbBuild)
$cbEnable = New-Object Windows.Forms.CheckBox
$cbEnable.Text = 'Включить плагин в проекте (запись в .uproject, резервная копия .uproject.bak)'
$cbEnable.Location = New-Object Drawing.Point(16, 236); $cbEnable.Size = New-Object Drawing.Size(690, 22); $cbEnable.Checked = $true
$form.Controls.Add($cbEnable)

$btnInstall = New-Object Windows.Forms.Button
$btnInstall.Text = 'Установить'; $btnInstall.Location = New-Object Drawing.Point(16, 270); $btnInstall.Size = New-Object Drawing.Size(160, 32)
$form.Controls.Add($btnInstall)
$btnRemove = New-Object Windows.Forms.Button
$btnRemove.Text = 'Удалить'; $btnRemove.Location = New-Object Drawing.Point(186, 270); $btnRemove.Size = New-Object Drawing.Size(120, 32)
$form.Controls.Add($btnRemove)
$btnClose = New-Object Windows.Forms.Button
$btnClose.Text = 'Закрыть'; $btnClose.Location = New-Object Drawing.Point(584, 270); $btnClose.Size = New-Object Drawing.Size(120, 32)
$form.Controls.Add($btnClose)

$tbLog = New-Object Windows.Forms.TextBox
$tbLog.Multiline = $true; $tbLog.ReadOnly = $true; $tbLog.ScrollBars = 'Vertical'; $tbLog.WordWrap = $true
$tbLog.Font = New-Object Drawing.Font('Consolas', 9)
$tbLog.Location = New-Object Drawing.Point(16, 314); $tbLog.Size = New-Object Drawing.Size(688, 230)
$form.Controls.Add($tbLog)

$logAction = { param($t) $tbLog.AppendText(([string]$t) + "`r`n"); [Windows.Forms.Application]::DoEvents() }
$pumpAction = { [Windows.Forms.Application]::DoEvents() }

function Get-SelectedEngineRoot {
	$i = $cbEngine.SelectedIndex
	if ($i -ge 0 -and $i -lt $engines.Count) { return $engines[$i].Path }
	return $null
}

function Select-EngineByPath([string]$Root) {
	for ($i = 0; $i -lt $engines.Count; $i++) { if ($engines[$i].Path -ieq $Root) { $cbEngine.SelectedIndex = $i; return $true } }
	return $false
}

function Update-Mode {
	$project = $rbProject.Checked
	$tbProject.Enabled = $project; $btnBrowse.Enabled = $project
	$cbBuild.Enabled = $project; $cbEnable.Enabled = $project
	if (-not $project) { $cbBuild.Checked = $true }
}

function Update-ProjectEngine {
	if (-not $rbProject.Checked) { return }
	$path = $tbProject.Text.Trim('"', ' ')
	if ($path -like '*.uproject' -and (Test-Path -LiteralPath $path)) {
		try {
			$root = Get-ProjectEngine $path
			if ($root -and (Select-EngineByPath $root)) { & $logAction "Движок проекта определён автоматически: $($cbEngine.SelectedItem)" }
			elseif (-not $root) { & $logAction 'Движок проекта определить не удалось - выберите его в списке.' }
		} catch { & $logAction ("Не удалось прочитать проект: " + $_.Exception.Message) }
	}
}

$rbProject.Add_CheckedChanged({ Update-Mode })
$rbEngine.Add_CheckedChanged({ Update-Mode })
$tbProject.Add_Leave({ Update-ProjectEngine })
$btnBrowse.Add_Click({
	$dialog = New-Object Windows.Forms.OpenFileDialog
	$dialog.Filter = 'Unreal project (*.uproject)|*.uproject'
	if ($dialog.ShowDialog() -eq 'OK') { $tbProject.Text = $dialog.FileName; Update-ProjectEngine }
})
$btnClose.Add_Click({ $form.Close() })

function Start-Action([bool]$Remove) {
	$mode = if ($rbProject.Checked) { 'Project' } else { 'Engine' }
	foreach ($c in @($btnInstall, $btnRemove, $btnBrowse, $btnClose, $rbProject, $rbEngine, $tbProject, $cbEngine, $cbBuild, $cbEnable)) { $c.Enabled = $false }
	try {
		$tbLog.Clear()
		Invoke-Install -Mode $mode -Project $tbProject.Text.Trim('"', ' ') -EngineRoot (Get-SelectedEngineRoot) `
			-Build $cbBuild.Checked -Enable $cbEnable.Checked -Remove $Remove -Log $logAction -Pump $pumpAction
	} catch {
		& $logAction ''
		& $logAction ('ОШИБКА: ' + $_.Exception.Message)
	} finally {
		foreach ($c in @($btnInstall, $btnRemove, $btnClose, $rbProject, $rbEngine, $cbEngine)) { $c.Enabled = $true }
		Update-Mode
	}
}
$btnInstall.Add_Click({ Start-Action $false })
$btnRemove.Add_Click({ Start-Action $true })

Update-Mode
if (-not $sourcePath) { $btnInstall.Enabled = $false }
[void]$form.ShowDialog()
