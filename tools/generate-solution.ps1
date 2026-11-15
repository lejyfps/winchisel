#Requires -Version 4.0
# Regenerates the local Winchisel.sln solution file, which is intentionally
# not committed to git (see .gitignore). Called by generate-solution.cmd.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$t = [char]9
$lines = @(
    'Microsoft Visual Studio Solution File, Format Version 12.00',
    '# Visual Studio Version 18',
    'VisualStudioVersion = 18.0.0.0',
    'MinimumVisualStudioVersion = 10.0.40219.1',
    'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "Winchisel.Core", "src\core\Winchisel.Core.vcxproj", "{A1C0E001-0001-4000-8000-000000000001}"',
    'EndProject',
    'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "Winchisel.Platform", "src\platform\Winchisel.Platform.vcxproj", "{A1C0E001-0002-4000-8000-000000000002}"',
    'EndProject',
    'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "Winchisel.App", "src\app\Winchisel.App.vcxproj", "{A1C0E001-0004-4000-8000-000000000004}"',
    'EndProject',
    'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "Winchisel.Updater", "src\updater\Winchisel.Updater.vcxproj", "{A1C0E001-0005-4000-8000-000000000005}"',
    'EndProject',
    'Global',
    ($t + 'GlobalSection(SolutionConfigurationPlatforms) = preSolution'),
    ($t + $t + 'Debug|x64 = Debug|x64'),
    ($t + $t + 'Release|x64 = Release|x64'),
    ($t + 'EndGlobalSection'),
    ($t + 'GlobalSection(ProjectConfigurationPlatforms) = postSolution'),
    ($t + $t + '{A1C0E001-0001-4000-8000-000000000001}.Debug|x64.ActiveCfg = Debug|x64'),
    ($t + $t + '{A1C0E001-0001-4000-8000-000000000001}.Debug|x64.Build.0 = Debug|x64'),
    ($t + $t + '{A1C0E001-0001-4000-8000-000000000001}.Release|x64.ActiveCfg = Release|x64'),
    ($t + $t + '{A1C0E001-0001-4000-8000-000000000001}.Release|x64.Build.0 = Release|x64'),
    ($t + $t + '{A1C0E001-0002-4000-8000-000000000002}.Debug|x64.ActiveCfg = Debug|x64'),
    ($t + $t + '{A1C0E001-0002-4000-8000-000000000002}.Debug|x64.Build.0 = Debug|x64'),
    ($t + $t + '{A1C0E001-0002-4000-8000-000000000002}.Release|x64.ActiveCfg = Release|x64'),
    ($t + $t + '{A1C0E001-0002-4000-8000-000000000002}.Release|x64.Build.0 = Release|x64'),
    ($t + $t + '{A1C0E001-0004-4000-8000-000000000004}.Debug|x64.ActiveCfg = Debug|x64'),
    ($t + $t + '{A1C0E001-0004-4000-8000-000000000004}.Debug|x64.Build.0 = Debug|x64'),
    ($t + $t + '{A1C0E001-0004-4000-8000-000000000004}.Release|x64.ActiveCfg = Release|x64'),
    ($t + $t + '{A1C0E001-0004-4000-8000-000000000004}.Release|x64.Build.0 = Release|x64'),
    ($t + $t + '{A1C0E001-0005-4000-8000-000000000005}.Debug|x64.ActiveCfg = Debug|x64'),
    ($t + $t + '{A1C0E001-0005-4000-8000-000000000005}.Debug|x64.Build.0 = Debug|x64'),
    ($t + $t + '{A1C0E001-0005-4000-8000-000000000005}.Release|x64.ActiveCfg = Release|x64'),
    ($t + $t + '{A1C0E001-0005-4000-8000-000000000005}.Release|x64.Build.0 = Release|x64'),
    ($t + 'EndGlobalSection'),
    ($t + 'GlobalSection(SolutionProperties) = preSolution'),
    ($t + $t + 'HideSolutionNode = FALSE'),
    ($t + 'EndGlobalSection'),
    'EndGlobal'
)
$sln = Join-Path $root 'Winchisel.sln'
[IO.File]::WriteAllText($sln, (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
Write-Output "Generated $sln"
