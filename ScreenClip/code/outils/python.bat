@echo off
set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
"%PY%" -c "import platformio, serial" >nul 2>nul
if not errorlevel 1 exit /b 0
set "PY=%USERPROFILE%\.screenclip\venv\Scripts\python.exe"
"%PY%" -c "import platformio, serial" >nul 2>nul
if not errorlevel 1 exit /b 0
echo Installation des outils ^(Python, PlatformIO^) : il faut internet, veuillez patienter quelques minutes.
set "UV=uv"
where uv >nul 2>nul
if errorlevel 1 (
  set "UV=%USERPROFILE%\.local\bin\uv.exe"
  if not exist "%USERPROFILE%\.local\bin\uv.exe" powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
)
"%UV%" venv --python 3.12 "%USERPROFILE%\.screenclip\venv"
if errorlevel 1 (
  echo Installation de Python impossible : verifie ta connexion internet, puis relance.
  exit /b 1
)
"%UV%" pip install --python "%PY%" platformio pyserial
if errorlevel 1 (
  echo Installation de PlatformIO impossible : verifie ta connexion internet, puis relance.
  exit /b 1
)
echo Installation terminee.
exit /b 0
