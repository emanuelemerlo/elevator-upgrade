# Builds and runs the tests with Visual Studio (cl), as the makefile does on Linux and macOS.
# To be run from a Visual Studio developer shell (ilammy/msvc-dev-cmd in the workflows).
$ErrorActionPreference = 'Stop'

$sources = Get-ChildItem src\*.cpp | Where-Object Name -ne 'Main.cpp' | ForEach-Object FullName
New-Item -ItemType Directory -Force -Path obj\msvc-tests | Out-Null

foreach ($test in Get-ChildItem tests\*.cpp) {
  $exe = "$($test.BaseName).exe"
  Write-Host "== $exe"
  cl /nologo /std:c++17 /EHsc /O1 /MT /W3 /Isrc /Foobj\msvc-tests\ $sources $test.FullName /Fe:$exe
  if ($LASTEXITCODE -ne 0) { throw "Build of $exe failed" }
  & ".\$exe"
  if ($LASTEXITCODE -ne 0) { throw "$exe failed" }
}
