pushd %~dp0\..
mkdir x86
mkdir x64
mkdir arm
cd x86
"cmake.exe" ../.. -DSTATIC_OUTPUT_DIR=%1
cmake --build ./ 

#cd ..
#cd x64
#"cmake.exe" -G "Visual Studio 14 2015 Win64" ..\..
#msbuild LiteCore.sln /p:Configuration=RelWithDebInfo /t:LiteCore

#cd ..
#cd arm
#"cmake.exe" -G "Visual Studio 14 2015 ARM" ..\..
#msbuild LiteCore.sln /p:Configuration=RelWithDebInfo /t:LiteCore
popd