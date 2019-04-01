echo off
set RGA_EXE=C:\Development\GitHub\RadeonGPUAnalyzer\RGA\Output\bin\Release\rga.exe
set OUTPUT_PATH=%%~df%%~pf%%~nf%%~xf_gcn
set OUTPUT_FILE_NOEXT=%OUTPUT_PATH%\%%~nf
set RGA_OPTIONS=-c TONGA -s hlsl -f main -p cs_5_0 --isa %OUTPUT_FILE_NOEXT%.isa --livereg %OUTPUT_FILE_NOEXT%.livereg -a %OUTPUT_FILE_NOEXT%.csv --il %OUTPUT_FILE_NOEXT%.il
for %%f in (data\shaders\*.hlsl) do (mkdir %OUTPUT_PATH%
, %RGA_EXE% %RGA_OPTIONS% %%f)