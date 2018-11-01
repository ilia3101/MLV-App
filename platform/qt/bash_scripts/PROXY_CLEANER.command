cat <<'EOF' > /tmp/proxy1.command
#!/bin/bash
#Processing MLV files into folders with dng files
#!/bin/bash
#entering the MLV folder. Place proxy MOV files next to your MLV or else this script will have no affect
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"

#progress_bar
open /tmp/progress_bar.command &

while ! [ x"$(cat /tmp/PROXYFILESaa)" = x ]
   do 
#Originals folder
    mkdir -p A_ORIGINALS
#check for proxy file
 if ls *.MOV >/dev/null 2>&1;
    then 
#Build matching structure
     BASE=$(cat /tmp/"PROXYFILESaa" | head -1 | rev | cut -d '/' -f1 | rev | cut -d "." -f1)
     MOV=$(cat /tmp/"PROXYFILESaa" | head -1 | rev | cut -d '/' -f1 | rev | cut -d "." -f1).MOV
  if [ -f *$MOV ]
     then
#Straight proxy making
      duration=$(exiftool *"$MOV" -b -MediaDuration)
   if (( $(echo "$duration < 5" |bc -l) )); 
      then
       snippet=$(echo 2)
      else
       snippet=$(echo 5)
   fi
      first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.02" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    if [ x"$first_black" = x ]; 
       then
        first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.04" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    if [ x"$first_black" = x ]; 
       then
        first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.08" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    fi
    fi
#producing the new cleaned proxy file
#safety check
    if ! [ x"$first_black" = x ]; 
      then
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -ss 0$first_black -i *"$MOV" -vcodec copy -acodec copy -timecode 00:00:00:00 "$out"n"${BASE}_1_".MOV
#move transcoded proxy to parent folder
mv -i *"$MOV" A_ORIGINALS
mv -i n"${BASE}_1_".MOV "${BASE}".MOV
 fi
  fi
    fi
#cut to the next name on the list
    echo "$(tail -n +2 /tmp/PROXYFILESaa)" > /tmp/PROXYFILESaa
   done
#done
rm /tmp/PROXYFILESaa
rm /tmp/proxy1.command
EOF

cat <<'EOF' > /tmp/proxy2.command
#!/bin/bash
#!/bin/bash
#entering the MLV folder.
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"
while ! [ x"$(cat /tmp/PROXYFILESab)" = x ]
   do 
#Originals folder
    mkdir -p A_ORIGINALS
#check for proxy file
 if ls *.MOV >/dev/null 2>&1;
    then 
#Build matching structure
     BASE=$(cat /tmp/"PROXYFILESab" | head -1 | rev | cut -d '/' -f1 | rev | cut -d "." -f1)
     MOV=$(cat /tmp/"PROXYFILESab" | head -1 | rev | cut -d '/' -f1 | rev | cut -d "." -f1).MOV
  if [ -f *$MOV ]
     then
#Straight proxy making
      duration=$(exiftool *"$MOV" -b -MediaDuration)
   if (( $(echo "$duration < 5" |bc -l) )); 
      then
       snippet=$(echo 2)
      else
       snippet=$(echo 5)
   fi
      first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.02" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    if [ x"$first_black" = x ]; 
       then
        first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.04" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    if [ x"$first_black" = x ]; 
       then
        first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.08" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    fi
    fi
#producing the new cleaned proxy file
#safety check
    if ! [ x"$first_black" = x ]; 
      then
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -ss 0$first_black -i *"$MOV" -vcodec copy -acodec copy -timecode 00:00:00:00 "$out"n"${BASE}_1_".MOV
#move transcoded proxy to parent folder
mv -i *"$MOV" A_ORIGINALS
mv -i n"${BASE}_1_".MOV "${BASE}".MOV
 fi
  fi
    fi
#cut to the next name on the list
    echo "$(tail -n +2 /tmp/PROXYFILESab)" > /tmp/PROXYFILESab
   done
#done
rm /tmp/PROXYFILESab
rm /tmp/proxy2.command
EOF


cat <<'EOF' > /tmp/proxy3.command
#!/bin/bash
#!/bin/bash
#entering the MLV folder.
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"
while ! [ x"$(cat /tmp/PROXYFILESac)" = x ]
   do 
#Originals folder
    mkdir -p A_ORIGINALS
#check for proxy file
 if ls *.MOV >/dev/null 2>&1;
    then 
#Build matching structure
     BASE=$(cat /tmp/"PROXYFILESac" | head -1 | rev | cut -d '/' -f1 | rev | cut -d "." -f1)
     MOV=$(cat /tmp/"PROXYFILESac" | head -1 | rev | cut -d '/' -f1 | rev | cut -d "." -f1).MOV
  if [ -f *$MOV ]
     then
#Straight proxy making
      duration=$(exiftool *"$MOV" -b -MediaDuration)
   if (( $(echo "$duration < 5" |bc -l) )); 
      then
       snippet=$(echo 2)
      else
       snippet=$(echo 5)
   fi
      first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.02" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    if [ x"$first_black" = x ]; 
       then
        first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.04" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    if [ x"$first_black" = x ]; 
       then
        first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.08" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    fi
    fi
#producing the new cleaned proxy file
#safety check
    if ! [ x"$first_black" = x ]; 
      then
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -ss 0$first_black -i *"$MOV" -vcodec copy -acodec copy -timecode 00:00:00:00 "$out"n"${BASE}_1_".MOV
#move transcoded proxy to parent folder
mv -i *"$MOV" A_ORIGINALS
mv -i n"${BASE}_1_".MOV "${BASE}".MOV
 fi
  fi
    fi
#cut to the next name on the list
    echo "$(tail -n +2 /tmp/PROXYFILESac)" > /tmp/PROXYFILESac
   done
#done
rm /tmp/PROXYFILESac
rm /tmp/proxy3.command
EOF

cat <<'EOF' > /tmp/proxy4.command
#!/bin/bash
#entering the MLV folder.
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"
#!/bin/bash
while ! [ x"$(cat /tmp/PROXYFILESad)" = x ]
   do 
#Originals folder
    mkdir -p A_ORIGINALS
#check for proxy file
 if ls *.MOV >/dev/null 2>&1;
    then 
#Build matching structure
     BASE=$(cat /tmp/"PROXYFILESad" | head -1 | rev | cut -d '/' -f1 | rev | cut -d "." -f1)
     MOV=$(cat /tmp/"PROXYFILESad" | head -1 | rev | cut -d '/' -f1 | rev | cut -d "." -f1).MOV
  if [ -f *$MOV ]
     then
#Straight proxy making
      duration=$(exiftool *"$MOV" -b -MediaDuration)
   if (( $(echo "$duration < 5" |bc -l) )); 
      then
       snippet=$(echo 2)
      else
       snippet=$(echo 5)
   fi
      first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.02" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    if [ x"$first_black" = x ]; 
       then
        first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.04" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    if [ x"$first_black" = x ]; 
       then
        first_black=$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i *"$MOV" -to $snippet -vf "blackdetect=d=0.1:pix_th=0.08" -an -f null - 2>&1 | grep -o "black_duration:.*" | cut -d ":" -f2)
    fi
    fi
#producing the new cleaned proxy file
#safety check
    if ! [ x"$first_black" = x ]; 
      then
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -ss 0$first_black -i *"$MOV" -vcodec copy -acodec copy -timecode 00:00:00:00 "$out"n"${BASE}_1_".MOV
#move transcoded proxy to parent folder
mv -i *"$MOV" A_ORIGINALS
mv -i n"${BASE}_1_".MOV "${BASE}".MOV
 fi
  fi
    fi
#cut to the next name on the list
    echo "$(tail -n +2 /tmp/PROXYFILESad)" > /tmp/PROXYFILESad
   done
#done
rm /tmp/PROXYFILESad
rm /tmp/proxy4.command
EOF

#proxymain command
cat <<'EOF' > /tmp/proxymain.command
#!/bin/bash

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
open /tmp/proxymain.command
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
. /tmp/proxymain.command
;;
esac
fi

chmod u=rwx /tmp/proxy1.command
chmod u=rwx /tmp/proxy2.command
chmod u=rwx /tmp/proxy3.command
chmod u=rwx /tmp/proxy4.command

#split into 4 chunks
   split -l $(( $( wc -l < /tmp/mlvapp_path/file_names.txt ) / 4 + 1 )) /tmp/mlvapp_path/file_names.txt /tmp/PROXYFILES 
   rm /tmp/PROXYFILES

sleep 1 && . /tmp/proxy1.command &
sleep 1 && . /tmp/proxy2.command &
sleep 1 && . /tmp/proxy3.command &
sleep 1 && . /tmp/proxy4.command &

sleep 1 && rm /tmp/proxymain.command & 

EOF

cat <<'EOF' > /tmp/progress_bar.command

#!/bin/bash
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"

patio="$PWD"

#PROXY CLEANER
printf '\e[8;10;24t'
printf '\e[3;955;0t'

clear
cat<<EOF1
     -------------
     $(tput setaf 0)$(tput bold)PROXY CLEANER$(tput sgr0)
     -------------
 $(tput bold)$(tput setaf 1)(C) CANCEL$(tput sgr0)
 $(tput bold)$(tput setaf 1)(e) Close this window$(tput sgr0)

     $(tput bold)Working.....

Selection:
EOF1

while sleep 1; 
do

if ! [ x"$dot5" = x ]
then
dot1=
dot2=
dot3=
dot4=
dot5=
fi

if ! [ x"$dot4" = x ]
then
dot5=$(echo .)
fi

if ! [ x"$dot3" = x ]
then
dot4=$(echo .)
fi

if ! [ x"$dot2" = x ]
then
dot3=$(echo .)
fi

if ! [ x"$dot1" = x ]
then
dot2=$(echo .)
fi

if [ x"$dot1" = x ]
then
dot1=$(echo .)
fi

cat<<EOF1
     -------------
     $(tput setaf 0)$(tput bold)PROXY CLEANER$(tput sgr0)
     -------------
 $(tput bold)$(tput setaf 1)(C) CANCEL$(tput sgr0)
 $(tput bold)$(tput setaf 1)(e) Close this window$(tput sgr0)

     $(tput bold)Working$dot1$dot2$dot3$dot4$dot5

Selection:
EOF1
if ! ls /tmp/PROXYFILESaa >/dev/null 2>&1;
then 
 if ! ls /tmp/PROXYFILESab >/dev/null 2>&1;
 then
   if ! ls /tmp/PROXYFILESac >/dev/null 2>&1;
   then
    if ! ls /tmp/PROXYFILESad >/dev/null 2>&1;
    then
afplay /System/Library/Sounds/Tink.aiff
sleep 0.5
afplay /System/Library/Sounds/Tink.aiff
sleep 0.5
afplay /System/Library/Sounds/Tink.aiff
sleep 0.5
echo -n -e "\033]0;PROXYwindow\007"
kill $(echo $$)
sleep 2 && rm /tmp/progress_bar.command &
osascript -e 'tell application "Terminal" to close (every window whose name contains "PROXYwindow")' & exit 
fi
 fi
  fi
   fi
done &
    read -n1
    case "$REPLY" in

    "C") 
echo > /tmp/KILLMOV
killall sleep
killall ffmpeg

sleep 2 && rm /tmp/progress_bar.command &
sleep 2 && rm /tmp/proxy*.command &
rm /tmp/PROXYFILES*
rm /tmp/proxy*.command
rm /tmp/KILLMOV
osascript -e 'tell application "Terminal" to close first window' & exit
;;

    "e") 
osascript -e 'tell application "Terminal" to close first window' & exit
;;

    "Q")  echo "case sensitive!!"   ;;
     * )  echo "invalid option"     ;;
    esac 
EOF

#open fps command and progress_bar.command
chmod u=rwx /tmp/proxymain.command
chmod u=rwx /tmp/progress_bar.command
chmod u=rwx "$(cat /tmp/mlvapp_path/app_path.txt | tr -d '"' )"/HDR_MOV.command

#check what´s missing
[ ! -f "/usr/local/bin/brew" ] && brew=$(echo brew missing)
[ ! -f "/usr/local/bin/exiftool" ] && exiftool=$(echo exiftool missing)

if [ x$brew$exiftool = x ]
then
sleep 0.2 && . /tmp/proxymain.command
fi

