@echo off
if not exist "%~dp0..\code\outils\flash.py" (
  echo Le dossier est incomplet : extrais d'abord tout le zip
  echo ^(clic droit sur le zip, Extraire tout^), puis relance ce fichier.
  goto end
)
cd /d "%~dp0..\code"
call outils\python.bat
if errorlevel 1 goto end
"%PY%" outils\flash.py
:end
echo.
pause
