m4_include(`farversion.m4')m4_dnl
version=MAJOR.MINOR.BUILD

!if "$(CPU)"=="AMD64" 
suffix=64
platform=x64
distrsuffix=_x64
!else
suffix=32
platform=x86
!endif

all:
  cd unicode_far
  nmake -f makefile_vc MP=/MP
  cd ..\misc\fexcept
  nmake -f makefile_vc WIDE=1 MP=/MP
  cd ..\..\plugins
  nmake -f makefile_all_vc WIDE=1 MP=/MP
  cd multiarc
  nmake -f makefile_vc MP=/MP
  cd ..\ftp
  nmake -f makefile_vc MP=/MP
  cd ..\..
  cl -nologo -O1 -GS- -EHsc installer\custom.cpp -link -dll -out:CustomActions.dll -export:UpdateFeatureState -export:Optimize msi.lib imagehlp.lib
  candle -nologo -Iinstaller -dSuffix=$(suffix) -dPlatform=$(platform) -dVersion=$(version) installer\installer.wxs installer\MyWixUI_FeatureTree.wxs
  light -nologo -ext WixUIExtension -cultures:en-us -sice:ICE61 -spdb -sval -dcl:high -dWixUIBannerBmp=installer\banner.bmp -dWixUIDialogBmp=installer\dialog.bmp -out far_$(version)$(distrsuffix)_bundle.msi installer.wixobj MyWixUI_FeatureTree.wixobj
