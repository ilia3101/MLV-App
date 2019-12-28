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

#Scripts for testing purposes
#alternative to tmix filter. Will enfuse/average the full content of a MLV file

#!/bin/bash
cat <<'EOF' > /tmp/enfuse_average.command

open -a terminal

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
. /tmp/enfuse_average.command
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
clear && echo "hugin did not install, let´s test reinstall"
/usr/local/bin/brew cask reinstall hugin
fi
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
. /tmp/enfuse_average.command
;;
esac
fi

#only works with tif files
if ! ls /tmp/mlvapp_path/tif_creation >/dev/null 2>&1; then
clear
echo "This script only works with sequenced tif exports"
exit 0
fi

clear
echo "Enfused files will be placed outside the tif folder"
sleep 2

#actual script engine
while grep 'MLV\|mlv' /tmp/mlvapp_path/file_names.txt; do
#folder path
  cd "$(cat /tmp/mlvapp_path/output_folder.txt | head -1)"
#file name
  cd "$(cat /tmp/mlvapp_path/file_names.txt | head -1 | cut -d "." -f1 | rev | cut -d "/" -f1 | rev)"
#align images and rename
  rm fps
  /Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned.tif *.tif
  /Applications/Hugin/tools_mac/enfuse -o ${PWD##*/}.tiff aligned.*
  mv ${PWD##*/}.tiff ../
  echo "$(tail -n +2 /tmp/mlvapp_path/file_names.txt)" > /tmp/mlvapp_path/file_names.txt

done
EOF

chmod u=rwx /tmp/enfuse_average.command
sleep 1
open /tmp/enfuse_average.command



