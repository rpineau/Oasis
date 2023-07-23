#!/bin/bash

PACKAGE_NAME="Oasis_X2.pkg"
BUNDLE_NAME="org.rti-zone.OasisX2"

if [ ! -z "$app_id_signature" ]; then
    codesign -f -s "$app_id_signature" --verbose ../build/Release/libOasis.dylib
fi

mkdir -p ROOT/tmp/Oasis_X2/
cp "../Oasis.ui" ROOT/tmp/Oasis_X2/
cp "../OasisFocuserSelect.ui" ROOT/tmp/Oasis_X2/
cp "../focuserlist Oasis.txt" ROOT/tmp/Oasis_X2/
cp "../build/Release/libOasis.dylib" ROOT/tmp/Oasis_X2/


if [ ! -z "$installer_signature" ]; then
	# signed package using env variable installer_signature
	pkgbuild --root ROOT --identifier $BUNDLE_NAME --sign "$installer_signature" --scripts Scripts --version 1.0 $PACKAGE_NAME
	pkgutil --check-signature ./${PACKAGE_NAME}

else
    pkgbuild --root ROOT --identifier $BUNDLE_NAME --scripts Scripts --version 1.0 $PACKAGE_NAME
fi

rm -rf ROOT
