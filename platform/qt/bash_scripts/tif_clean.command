#GNU public license

#This program is free software; you can redistribute it and/or
 # modify it under the terms of the GNU General Public License
 # as published by the Free Software Foundation; either version 2
 # of the License, or (at your option) any later version.
 # 
 # This program is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 # GNU General Public License for more details.
 # 
 # You should have received a copy of the GNU General Public License
 # along with this program; if not, write to the
 # Free Software Foundation, Inc.,
 # 51 Franklin Street, Fifth Floor,
 # Boston, MA  02110-1301, USA.

#aliasing and grain cleaning script working through badass program hugin align_image_stack and enfuse
#the script uses 5 files to average/enfuse into one file then moves forward to the consecutive frame. Surrounded tifs are used. E.g tif1,tif2,tifinput,tif3,tif4
#final output is a high quality prores file


#start clean
rm /tmp/TIFCLEAN
rm /tmp/tif_clean.command

#!/bin/bash
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"
#log
rm "$(cat /tmp/mlvapp_path/output_folder.txt)"/TIFCLEAN_LOG.txt
echo "$(date)" >> TIFCLEAN_LOG.txt
echo "##################tif_clean.command#####################" >> TIFCLEAN_LOG.txt
echo "" >> TIFCLEAN_LOG.txt
echo "###Checking for paths###" >> TIFCLEAN_LOG.txt
echo outputpath: "$(cat /tmp/mlvapp_path/output_folder.txt)" >> TIFCLEAN_LOG.txt
echo applicationpath: "$(cat /tmp/mlvapp_path/app_path.txt)" >> TIFCLEAN_LOG.txt
echo "" >> TIFCLEAN_LOG.txt
echo "###Checking for dependencies###" >> TIFCLEAN_LOG.txt
if [ -f "/usr/local/bin/brew" ]
then
echo "brew: /usr/local/bin/brew" >> TIFCLEAN_LOG.txt
else
echo "brew: MISSING!" >> TIFCLEAN_LOG.txt
fi
if [ -f "/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack" ]
then
echo "align_image_stack: /Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack" >> TIFCLEAN_LOG.txt
else
echo "align_image_stack: MISSING!" >> TIFCLEAN_LOG.txt
fi
if [ -f "/usr/local/bin/exiftool" ]
then
echo "exiftool: /usr/local/bin/exiftool" >> TIFCLEAN_LOG.txt
else
echo "exiftool: MISSING!" >> TIFCLEAN_LOG.txt
fi
echo ""  >> TIFCLEAN_LOG.txt
echo "take note that if you run the script for the first time
dependencies will naturally be missing in this log file"  >> TIFCLEAN_LOG.txt

###########dependencies############
#homebrew
if ! [ -f "/usr/local/bin/brew" ]
then
printf '\e[8;16;85t'
printf '\e[3;410;100t'
clear
read -p $(tput bold)"homebrew is not installed would you like to install it?$(tput setaf 1)
(Y/N)?$(tput sgr0)
" choice
case "$choice" in 
  y|Y ) 
#!/bin/bash
clear
echo "Follow instructions in terminal window"
sleep 2
/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
if [ -f "/usr/local/bin/brew" ]
then
clear && echo "brew is intalled and ready for use"
else
clear && echo "brew did not install"
fi
sleep 2
;;
  n|N ) 
;;
  * ) 
echo "invalid selection, let´s start again"
sleep 1
. "$(cat /tmp/mlvapp_path/app_path.txt | tr -d '"' )"/tif_clean.command
;;
esac
fi

#hugin
if ! [ -f "/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack" ]
then
printf '\e[8;16;85t'
printf '\e[3;410;100t'
clear
echo $(tput bold)"
Checking for hugin, please wait..."
sleep 2
read -p $(tput bold)"hugin is not installed would you like to install it?$(tput setaf 1)
(Y/N)?$(tput sgr0)
" choice
case "$choice" in 
  y|Y ) 
#!/bin/bash
clear
echo "Follow instructions in terminal window"
sleep 2
/usr/local/bin/brew cask install hugin
if [ -f "/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack" ]
then
clear && echo "hugin is intalled and ready for use"
else
clear && echo "hugin did not install"
fi
sleep 2
;;
  n|N ) 
clear
;;
  * ) 
echo "invalid selection, let´s start again"
sleep 1
. "$(cat /tmp/mlvapp_path/app_path.txt | tr -d '"' )"/tif_clean.command
;;
esac
fi

#exiftool
if ! [ -f "/usr/local/bin/exiftool" ]
then
printf '\e[8;16;85t'
printf '\e[3;410;100t'
clear
echo $(tput bold)"
Checking for exiftool, please wait..."
sleep 2
read -p $(tput bold)"exiftool is not installed would you like to install it?$(tput setaf 1)
(Y/N)?$(tput sgr0)
" choice
case "$choice" in 
  y|Y ) 
#!/bin/bash
clear
echo "Follow instructions in terminal window"
sleep 2
/usr/local/bin/brew install exiftool
if [ -f "/usr/local/bin/exiftool" ]
then
clear && echo "exiftool is intalled and ready for use"
else
clear && echo "exiftool did not install"
fi
sleep 2
;;
  n|N ) 
clear
;;
  * ) 
echo "invalid selection, let´s start again"
sleep 1
. "$(cat /tmp/mlvapp_path/app_path.txt | tr -d '"' )"/tif_clean.command
;;
esac
fi

#list files for multiprocessing
#first check for tif creation
if [ -f /tmp/mlvapp_path/tif_creation ]
then
rm /tmp/mlvapp_path/tif_creation
find -s . -maxdepth 2 -name '*.tif' -print0 | xargs -0 -n1 dirname | sort --unique > /tmp/TIFCLEAN
else
ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} > /tmp/TIFCLEAN
fi


cat <<'EOF' > /tmp/tif_clean.command
while grep 'MOV\|mov\|mp4\|MP4\|mkv\|MKV\|avi\|AVI\|./' /tmp/TIFCLEAN; do
#enter output folder
#LOG
#log file
exec &> >(tee -a "$(cat /tmp/mlvapp_path/output_folder.txt)"/TIFCLEAN_LOG.txt >&2 )
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"
#build a temp folder only if it´s not a folder
if ! grep './' /tmp/TIFCLEAN
then
mkdir -p $(cat /tmp/TIFCLEAN | head -1 | cut -d "." -f1)
mv $(cat /tmp/TIFCLEAN | head -1) $(cat /tmp/TIFCLEAN | head -1 | cut -d "." -f1)
cd "$(cat /tmp/TIFCLEAN | head -1 | cut -d "." -f1)"
#spit out tif files
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(cat /tmp/TIFCLEAN | head -1) -pix_fmt rgb24 %06d.tif
else
#seems we have tif folders
cd "$(cat /tmp/TIFCLEAN | head -1 | cut -d '/' -f2)"
fi

#tif_clean.command starts here
#first 2 files
#1
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned_01.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned_01.tif0000.tif aligned_01.tif0000.tif aligned_01.tif0000.tif aligned_01.tif0000.tif aligned_01.tif0001.tif aligned_01.tif0002.tif aligned_01.tif0003.tif aligned_01.tif0004.tif && \
rm aligned_01.tif0000.tif aligned_01.tif0001.tif aligned_01.tif0002.tif aligned_01.tif0003.tif aligned_01.tif0004.tif 

mv $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1' | cut -d "." -f2 | cut -d "/" -f2).tiff $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1' | cut -d "." -f2 | cut -d "/" -f2).tif
cp $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1' | cut -d "." -f2 | cut -d "/" -f2).tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1' | cut -d "." -f2 | cut -d "/" -f2).tiff

#2
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned_02.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned_02.tif0000.tif aligned_02.tif0000.tif aligned_02.tif0000.tif aligned_02.tif0000.tif aligned_02.tif0001.tif aligned_02.tif0002.tif aligned_02.tif0003.tif aligned_02.tif0004.tif && \
rm aligned_02.tif0000.tif aligned_02.tif0001.tif aligned_02.tif0002.tif aligned_02.tif0003.tif aligned_02.tif0004.tif 

mv $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2' | cut -d "." -f2 | cut -d "/" -f2).tiff $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2' | cut -d "." -f2 | cut -d "/" -f2).tif
cp $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2' | cut -d "." -f2 | cut -d "/" -f2).tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2' | cut -d "." -f2 | cut -d "/" -f2).tiff

#let´s do the rest 
while grep 'tif' <<< $(find -s . -maxdepth 1 -iname '*.tif')
do
if ! (( $(find -s . -maxdepth 1 -name '*.tif' | wc -l) < 6 ))
then 
#1
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned.tif0000.tif aligned.tif0000.tif aligned.tif0000.tif aligned.tif0000.tif aligned.tif0001.tif aligned.tif0002.tif aligned.tif0003.tif aligned.tif0004.tif && \
rm aligned.tif0000.tif aligned.tif0001.tif aligned.tif0002.tif aligned.tif0003.tif aligned.tif0004.tif &


#2
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned2.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0001.tif aligned2.tif0002.tif aligned2.tif0003.tif aligned2.tif0004.tif && \
rm aligned2.tif0000.tif aligned2.tif0001.tif aligned2.tif0002.tif aligned2.tif0003.tif aligned2.tif0004.tif &


#3
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned3.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned3.tif0000.tif aligned3.tif0000.tif aligned3.tif0000.tif aligned3.tif0000.tif  aligned3.tif0001.tif aligned3.tif0002.tif aligned3.tif0003.tif aligned3.tif0004.tif && \
rm aligned3.tif0000.tif aligned3.tif0001.tif aligned3.tif0002.tif aligned3.tif0003.tif aligned3.tif0004.tif &


#4
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned4.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 8') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned4.tif0000.tif aligned4.tif0000.tif aligned4.tif0000.tif aligned4.tif0000.tif aligned4.tif0001.tif aligned4.tif0002.tif aligned4.tif0003.tif aligned4.tif0004.tif && \
rm aligned4.tif0000.tif aligned4.tif0001.tif aligned4.tif0002.tif aligned4.tif0003.tif aligned4.tif0004.tif &


#5
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned5.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 8') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 9') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned5.tif0000.tif aligned5.tif0000.tif aligned5.tif0000.tif aligned5.tif0000.tif aligned5.tif0001.tif aligned5.tif0002.tif aligned5.tif0003.tif aligned5.tif0004.tif && \
rm aligned5.tif0000.tif aligned5.tif0001.tif aligned5.tif0002.tif aligned5.tif0003.tif aligned5.tif0004.tif &

#wait for jobs to end
    wait < <(jobs -p)

rm aligned*.tif

mv $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3' | cut -d "." -f2 | cut -d "/" -f2).tiff $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3' | cut -d "." -f2 | cut -d "/" -f2).tif
mv $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | cut -d "/" -f2).tiff $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | cut -d "/" -f2).tif
mv $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5' | cut -d "." -f2 | cut -d "/" -f2).tiff $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5' | cut -d "." -f2 | cut -d "/" -f2).tif
mv $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6' | cut -d "." -f2 | cut -d "/" -f2).tiff $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6' | cut -d "." -f2 | cut -d "/" -f2).tif
mv $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7' | cut -d "." -f2 | cut -d "/" -f2).tiff $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7' | cut -d "." -f2 | cut -d "/" -f2).tif


#2nd pass

#1
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned.tif0000.tif aligned.tif0000.tif aligned.tif0000.tif aligned.tif0000.tif aligned.tif0001.tif aligned.tif0002.tif aligned.tif0003.tif aligned.tif0004.tif && \
rm aligned.tif0000.tif aligned.tif0001.tif aligned.tif0002.tif aligned.tif0003.tif aligned.tif0004.tif &


#2
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned2.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0001.tif aligned2.tif0002.tif aligned2.tif0003.tif aligned2.tif0004.tif && \
rm aligned2.tif0000.tif aligned2.tif0001.tif aligned2.tif0002.tif aligned2.tif0003.tif aligned2.tif0004.tif &


#3
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned3.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned3.tif0000.tif aligned3.tif0000.tif aligned3.tif0000.tif aligned3.tif0000.tif  aligned3.tif0001.tif aligned3.tif0002.tif aligned3.tif0003.tif aligned3.tif0004.tif && \
rm aligned3.tif0000.tif aligned3.tif0001.tif aligned3.tif0002.tif aligned3.tif0003.tif aligned3.tif0004.tif &


#4
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned4.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 8') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned4.tif0000.tif aligned4.tif0000.tif aligned4.tif0000.tif aligned4.tif0000.tif aligned4.tif0001.tif aligned4.tif0002.tif aligned4.tif0003.tif aligned4.tif0004.tif && \
rm aligned4.tif0000.tif aligned4.tif0001.tif aligned4.tif0002.tif aligned4.tif0003.tif aligned4.tif0004.tif &


#5
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned5.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 8') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 9') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 7' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned5.tif0000.tif aligned5.tif0000.tif aligned5.tif0000.tif aligned5.tif0000.tif aligned5.tif0001.tif aligned5.tif0002.tif aligned5.tif0003.tif aligned5.tif0004.tif && \
rm aligned5.tif0000.tif aligned5.tif0001.tif aligned5.tif0002.tif aligned5.tif0003.tif aligned5.tif0004.tif &

#wait for jobs to end
    wait < <(jobs -p)

rm aligned*.tif
rm $(find -s . -maxdepth 1 -name '*.tif' | head -5)

else

#last batch of files
num=$(find -s . -maxdepth 1 -name '*.tif' | wc -l)

if [ $num = 4 ]
then 
#1
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned2.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif \
aligned2.tif0001.tif aligned2.tif0002.tif aligned2.tif0003.tif && \
rm aligned2.tif0000.tif aligned2.tif0001.tif aligned2.tif0002.tif aligned2.tif0003.tif

#wait for jobs to end
    wait < <(jobs -p)
rm aligned*.tif
rm $(find -s . -maxdepth 1 -name '*.tif' | head -4)
fi

if [ $num = 5 ]
then 
#1
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned.tif0000.tif aligned.tif0000.tif aligned.tif0000.tif aligned.tif0000.tif aligned.tif0001.tif aligned.tif0002.tif aligned.tif0003.tif aligned.tif0004.tif && \
rm aligned.tif0000.tif aligned.tif0001.tif aligned.tif0002.tif aligned.tif0003.tif aligned.tif0004.tif &

#2
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack --use-given-order -a aligned2.tif \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') \
$(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') \
&& /Applications/Hugin/tools_mac/enfuse -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | cut -d "/" -f2).tiff \
aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0000.tif aligned2.tif0001.tif aligned2.tif0002.tif aligned2.tif0003.tif && \
rm aligned2.tif0000.tif aligned2.tif0001.tif aligned2.tif0002.tif aligned2.tif0003.tif &

#wait for jobs to end
    wait < <(jobs -p)
rm aligned*.tif
rm $(find -s . -maxdepth 1 -name '*.tif' | head -5)
fi

#wait for jobs to end
    wait < <(jobs -p)

#last file
for i in *.tif ; 
do
    mv "$i" "${i/.tif}".tiff
done

if grep 'tif' <<< $(find -s . -maxdepth 1 -name '*.tif')
then
rm $(find -s . -maxdepth 1 -name '*.tif' | head -5)
fi
rm aligned*.tif
fi

done
#let´s go back 
cd -
#enter the next file on the list
echo "$(tail -n +2 /tmp/TIFCLEAN)" > /tmp/TIFCLEAN
done


#send the folder to a ffmpeg output list
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"
find -s . -maxdepth 2 -name '*.tiff' -print0 | xargs -0 -n1 dirname | sort --unique > /tmp/TIFCLEAN
#check for tif folders
while grep './' /tmp/TIFCLEAN
do
cd "$(cat /tmp/TIFCLEAN | head -1 | cut -d '/' -f2)"
#check for audio
wav=
acodec=
if ! [ x"$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | head -1) 2>&1 | grep Audio)" = x ]
then 
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | head -1) -vn -acodec copy $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | head -1 | cut -d "." -f1).wav
wav=$(printf "%s\n" -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | head -1 | cut -d "." -f1).wav)
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi
#tif folders
if ls *.wav
then
wav=$(printf "%s\n" -i $(ls *.wav | awk 'FNR == 1'))
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi
if ! [ x"$(ls *.mov)" = x ] || ! [ x"$(ls *.MOV)" = x ]
then
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(/usr/local/bin/exiftool $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | head -1) | grep 'Video Frame Rate' | cut -d ":" -f2) -i %06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le "$(cat /tmp/mlvapp_path/output_folder.txt)"/$(cat /tmp/TIFCLEAN | head -1 | cut -d '/' -f2 | cut -d "." -f1).mov
else
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(cat fps) -i "$(cat /tmp/TIFCLEAN | head -1 | cut -d '/' -f2 | cut -d "." -f1)"_%06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le "$(cat /tmp/mlvapp_path/output_folder.txt)"/$(cat /tmp/TIFCLEAN | head -1 | cut -d '/' -f2 | cut -d "." -f1).mov 
fi
#check for matching MLV or else do not remove
check="$(cat /tmp/TIFCLEAN | head -1 | cut -d '/' -f2 | cut -d "." -f1)".MLV
if grep "$check" <<< $(cat /tmp/mlvapp_path/file_names.txt)
then
if grep './' /tmp/TIFCLEAN
then
rm -r ../$(cat /tmp/TIFCLEAN | head -1 | cut -d '/' -f2 | cut -d "." -f1)
fi
else
mv *.{mov,MOV} "$(cat /tmp/mlvapp_path/output_folder.txt)"/
fi
#enter the next file on the list
echo "$(tail -n +2 /tmp/TIFCLEAN)" > /tmp/TIFCLEAN
#get back out into parent folder again
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"
done
#all done!
rm /tmp/tif_clean.command
EOF

chmod u=rwx /tmp/tif_clean.command
open /tmp/tif_clean.command


