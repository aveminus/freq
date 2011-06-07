#!/bin/bash
set -e

urls=

read -p "Rebuild all code? (Y/n) " simpleclean; echo
if [ "${simpleclean}" == "y" ] || [ "${simpleclean}" == "Y" ]; then
simpleclean=
fi

INPUT=licensees.txt
OLDIFS=$IFS

# IFS is set to 'tab'
IFS="	"

[ ! -f $INPUT ] && { echo "$INPUT file not found"; exit 99; }
while read personalemail personallicensetype personalexpired target
do
	IFS=$OLDIFS
	. ./deploy.sh; urls="${urls}\n$url"
	simpleclean=nonempty
	IFS="	"
done < $INPUT
IFS=$OLDIFS


echo "========================== All urls ==========================="
echo -e $urls

