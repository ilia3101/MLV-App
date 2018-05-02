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
#Drop HDR MOV files into a folder and run the script i terminal from within the folder with MOV files. Hit enter when prompted

#!/bin/bash
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"
#log
rm "$(cat /tmp/mlvapp_path/output_folder.txt)"/HDRMOV_LOG.txt
echo "$(date)" >> HDRMOV_LOG.txt
echo "##################HDR_MOV.command#####################" >> HDRMOV_LOG.txt
echo "More logs to be found in here /tmp/HDRMOV_LOGS" >> HDRMOV_LOG.txt
echo "" >> HDRMOV_LOG.txt
echo "###Checking for paths###" >> HDRMOV_LOG.txt
echo outputpath: "$(cat /tmp/mlvapp_path/output_folder.txt)" >> HDRMOV_LOG.txt
echo applicationpath: "$(cat /tmp/mlvapp_path/app_path.txt)" >> HDRMOV_LOG.txt
echo "" >> HDRMOV_LOG.txt
echo "###Checking for dependencies###" >> HDRMOV_LOG.txt
if [ -f "/usr/local/bin/brew" ]
then
echo "brew: /usr/local/bin/brew" >> HDRMOV_LOG.txt
else
echo "brew: MISSING!" >> HDRMOV_LOG.txt
fi
if [ -f "/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack" ]
then
echo "align_image_stack: /Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack" >> HDRMOV_LOG.txt
else
echo "align_image_stack: MISSING!" >> HDRMOV_LOG.txt
fi
if [ -f "/usr/local/bin/exiftool" ]
then
echo "exiftool: /usr/local/bin/exiftool" >> HDRMOV_LOG.txt
else
echo "exiftool: MISSING!" >> HDRMOV_LOG.txt
fi
echo ""  >> HDRMOV_LOG.txt
echo "take note that if you run the script for the first time
dependencies will naturally be missing in this log file"  >> HDRMOV_LOG.txt

rm /tmp/HDRMOV*
rm /tmp/HDR_script*.command
rm /tmp/KILLMOV
rm /tmp/progress_bar.command
 
#list files for multiprocessing
#first check for tif creation
if [ -f /tmp/mlvapp_path/tif_creation ]
then
rm /tmp/mlvapp_path/tif_creation
find . -maxdepth 2 -name '*.tif' -print0 | xargs -0 -n1 dirname | sort --unique | grep -v HDR_ORIGINALS > /tmp/HDRMOV
split -l $(( $( wc -l < /tmp/HDRMOV ) / 4 + 1 )) /tmp/HDRMOV /tmp/HDRMOV
rm /tmp/HDRMOV
else
ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' > /tmp/HDRMOV
split -l $(( $( wc -l < /tmp/HDRMOV ) / 4 + 1 )) /tmp/HDRMOV /tmp/HDRMOV
rm /tmp/HDRMOV
fi


if grep 'MOV\|mov\|mp4\|MP4\|mkv\|MKV\|avi\|AVI\|./' /tmp/HDRMOVaa
then
cat <<'EOF' > /tmp/HDR_script.command
#!/bin/bash
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"

#progress_bar
open /tmp/progress_bar.command &

#log
echo "" >> HDRMOV_LOG.txt
echo "##################HDR_script.command#####################" >> HDRMOV_LOG.txt
#run the log file

while grep 'MOV\|mov\|mp4\|MP4\|mkv\|MKV\|avi\|AVI\|./' /tmp/HDRMOVaa; do
#build a temp folder only if it´s not a folder
if ! grep './' /tmp/HDRMOVaa
then
mkdir -p $(cat /tmp/HDRMOVaa | head -1 | cut -d "." -f1)
mv $(cat /tmp/HDRMOVaa | head -1) $(cat /tmp/HDRMOVaa | head -1 | cut -d "." -f1)
cd "$(cat /tmp/HDRMOVaa | head -1 | cut -d "." -f1)"
#spit out tif files
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(cat /tmp/HDRMOVaa | head -1) -pix_fmt rgb24 %06d.tif
else
#seems we have tif folders
cd "$(cat /tmp/HDRMOVaa | head -1 | cut -d '/' -f2)"
fi

#crop and rescale is needed is needed after aligning. Will take place in #output cropped and aligned images section
#check for tif folders
if ! grep './' /tmp/HDRMOVaa
then
cr_W=$(echo $(/usr/local/bin/exiftool $(cat /tmp/HDRMOVaa | head -1) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f1 ))
cr_H=$(echo $(/usr/local/bin/exiftool $(cat /tmp/HDRMOVaa | head -1) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f2 ))
else
cr_W=$(echo $(/usr/local/bin/exiftool $(echo *_000000.tif) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f1 ))
cr_H=$(echo $(/usr/local/bin/exiftool $(echo *_000000.tif) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f2 ))
fi
cr_Ws=$(echo $cr_W*0.98 | bc -l | cut -d "." -f1)
cr_Hs=$(echo $cr_H*0.98 | bc -l | cut -d "." -f1)
crp_fix=$(echo crop=$cr_Ws:$cr_Hs,scale=$cr_W:$cr_H)

#First script combines enfused and aligned tif files then exports it to a prores mov file
while grep -E "tif" <<< $(find . -maxdepth 1 -iname '*.tif')
do
#align images and rename
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') && mv aligned.tif0000.tif 1.tiff && mv aligned.tif0001.tif 2.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned2.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') && mv aligned2.tif0000.tif 01.tiff && mv aligned2.tif0001.tif 02.tiff & 

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned3.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') && mv aligned3.tif0000.tif 001.tiff && mv aligned3.tif0001.tif 002.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned4.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') && mv aligned4.tif0000.tif 0001.tiff && mv aligned4.tif0001.tif 0002.tiff & 

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned5.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') && mv aligned5.tif0000.tif 00001.tiff && mv aligned5.tif0001.tif 00002.tiff & 

#wait for jobs to end
    wait < <(jobs -p)

#if killing process
if ! ls /tmp/KILLMOV 
then 
#enfuse workflow. Needs crop to care for align processing. Roundtrip because of enfuse interpolation causing flicker otherwise
#output cropped and aligned images
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 00001.tiff -pix_fmt rgb24 -vf $crp_fix 00001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 00002.tiff -pix_fmt rgb24 -vf $crp_fix 00002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 0001.tiff -pix_fmt rgb24 -vf $crp_fix 0001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 0002.tiff -pix_fmt rgb24 -vf $crp_fix 0002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 001.tiff -pix_fmt rgb24 -vf $crp_fix 001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 002.tiff -pix_fmt rgb24 -vf $crp_fix 002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 01.tiff -pix_fmt rgb24 -vf $crp_fix 01b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 02.tiff -pix_fmt rgb24 -vf $crp_fix 02b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 1.tiff -pix_fmt rgb24 -vf $crp_fix 1b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 2.tiff -pix_fmt rgb24 -vf $crp_fix 2b.tiff &
#wait for jobs to end
    wait < <(jobs -p)

/Applications/Hugin/tools_mac/enfuse 1b.tiff 2b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 01b.tiff 02b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 001b.tiff 002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 0001b.tiff 0002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 00001b.tiff 00002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5' | cut -d "." -f2 | tr -d "/").tiff & 
#wait for jobs to end
    wait < <(jobs -p)
#remove unwanted files
rm 00001.tiff 00002.tiff 0001.tiff 0002.tiff 001.tiff 002.tiff 01.tiff 02.tiff 1.tiff 2.tiff
rm 00001b.tiff 00002b.tiff 0001b.tiff 0002b.tiff 001b.tiff 002b.tiff 01b.tiff 02b.tiff 1b.tiff 2b.tiff
rm $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1')
fi
done

#check for audio
if ! [ x"$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) 2>&1 | grep Audio)" = x ]
then 
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) -vn -acodec copy $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1 | cut -d "." -f1).wav
wav=$(printf "%s\n" -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1 | cut -d "." -f1).wav)
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi
#tif folders
if ls *.wav
then
wav=$(printf "%s\n" -i $(ls *.wav | awk 'FNR == 1'))
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi

#output to prores
echo "" >> HDRMOV_LOG.txt
echo "##################ffmpeg output script#####################" >> HDRMOV_LOG.txt
#check for tif folders
if ! grep './' /tmp/HDRMOVaa
then
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(/usr/local/bin/exiftool $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) | grep 'Video Frame Rate' | cut -d ":" -f2) -i %06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le ../HDR_$(cat /tmp/HDRMOVaa | head -1 | cut -d "." -f1).mov 2>> "$(cat /tmp/mlvapp_path/output_folder.txt)"/HDRMOV_LOG.txt
#remove tiff files and HDR mov when done
#check for matching MLV or else do not remove
check="$(cat /tmp/HDRMOVaa | head -1 | cut -d "." -f1)".MLV
if grep "$check" <<< $(cat /tmp/mlvapp_path/file_names.txt)
then
rm -r ../$(cat /tmp/HDRMOVaa | head -1 | cut -d "." -f1)
else
mv *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} ../
rm -r ../$(cat /tmp/HDRMOVaa | head -1 | cut -d "." -f1)
fi
else
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(cat fps) -i "$(cat /tmp/HDRMOVaa | head -1 | cut -d '/' -f2 | cut -d "." -f1)"_%06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le ../HDR_$(cat /tmp/HDRMOVaa | head -1 | cut -d '/' -f2 | cut -d "." -f1).mov 2>> "$(cat /tmp/mlvapp_path/output_folder.txt)"/HDRMOV_LOG.txt
#remove tiff files when done
#check for matching MLV or else do not remove
check="$(cat /tmp/HDRMOVaa | head -1 | cut -d '/' -f2 | cut -d "." -f1)".MLV
if grep "$check" <<< $(cat /tmp/mlvapp_path/file_names.txt)
then
rm -r ../$(cat /tmp/HDRMOVaa | head -1 | cut -d '/' -f2 | cut -d "." -f1)
fi
fi
#let´s go back 
cd -
if ls /tmp/KILLMOV 
then 
rm /tmp/HDRMOVaa
fi
echo "$(tail -n +2 /tmp/HDRMOVaa)" > /tmp/HDRMOVaa
done
rm /tmp/HDRMOVaa
rm /tmp/HDR_script.command
EOF
fi


if grep 'MOV\|mov\|mp4\|MP4\|mkv\|MKV\|avi\|AVI\|./' /tmp/HDRMOVab
then
cat <<'EOF' > /tmp/HDR_script1.command
#!/bin/bash
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"

#log
echo "##################HDR_script1.command#####################" >> /tmp/HDRMOV_LOGS/HDR_script1_LOG.txt
#run the log file

while grep 'MOV\|mov\|mp4\|MP4\|mkv\|MKV\|avi\|AVI\|./' /tmp/HDRMOVab; do
#build a temp folder only if it´s not a folder
if ! grep './' /tmp/HDRMOVab
then
mkdir -p $(cat /tmp/HDRMOVab | head -1 | cut -d "." -f1)
mv $(cat /tmp/HDRMOVab | head -1) $(cat /tmp/HDRMOVab | head -1 | cut -d "." -f1)
cd "$(cat /tmp/HDRMOVab | head -1 | cut -d "." -f1)"
#spit out tif files
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(cat /tmp/HDRMOVab | head -1) -pix_fmt rgb24 %06d.tif
else
#seems we have tif folders
cd "$(cat /tmp/HDRMOVab | head -1 | cut -d '/' -f2)"
fi

#crop and rescale is needed is needed after aligning. Will take place in #output cropped and aligned images section
#check for tif folders
if ! grep './' /tmp/HDRMOVab
then
cr_W=$(echo $(/usr/local/bin/exiftool $(cat /tmp/HDRMOVab | head -1) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f1 ))
cr_H=$(echo $(/usr/local/bin/exiftool $(cat /tmp/HDRMOVab | head -1) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f2 ))
else
cr_W=$(echo $(/usr/local/bin/exiftool $(echo *_000000.tif) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f1 ))
cr_H=$(echo $(/usr/local/bin/exiftool $(echo *_000000.tif) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f2 ))
fi
cr_Ws=$(echo $cr_W*0.98 | bc -l | cut -d "." -f1)
cr_Hs=$(echo $cr_H*0.98 | bc -l | cut -d "." -f1)
crp_fix=$(echo crop=$cr_Ws:$cr_Hs,scale=$cr_W:$cr_H)

#First script combines enfused and aligned tif files then exports it to a prores mov file

while grep -E "tif" <<< $(find . -maxdepth 1 -iname '*.tif')
do
#align images and rename
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') && mv aligned.tif0000.tif 1.tiff && mv aligned.tif0001.tif 2.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned2.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') && mv aligned2.tif0000.tif 01.tiff && mv aligned2.tif0001.tif 02.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned3.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') && mv aligned3.tif0000.tif 001.tiff && mv aligned3.tif0001.tif 002.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned4.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') && mv aligned4.tif0000.tif 0001.tiff && mv aligned4.tif0001.tif 0002.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned5.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') && mv aligned5.tif0000.tif 00001.tiff && mv aligned5.tif0001.tif 00002.tiff &

#wait for jobs to end
    wait < <(jobs -p)

#if killing process
if ! ls /tmp/KILLMOV 
then 
#enfuse workflow. Needs crop to care for align processing. Roundtrip because of enfuse interpolation causing flicker otherwise
#output cropped and aligned images
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 00001.tiff -pix_fmt rgb24 -vf $crp_fix 00001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 00002.tiff -pix_fmt rgb24 -vf $crp_fix 00002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 0001.tiff -pix_fmt rgb24 -vf $crp_fix 0001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 0002.tiff -pix_fmt rgb24 -vf $crp_fix 0002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 001.tiff -pix_fmt rgb24 -vf $crp_fix 001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 002.tiff -pix_fmt rgb24 -vf $crp_fix 002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 01.tiff -pix_fmt rgb24 -vf $crp_fix 01b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 02.tiff -pix_fmt rgb24 -vf $crp_fix 02b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 1.tiff -pix_fmt rgb24 -vf $crp_fix 1b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 2.tiff -pix_fmt rgb24 -vf $crp_fix 2b.tiff &
#wait for jobs to end
    wait < <(jobs -p)

/Applications/Hugin/tools_mac/enfuse 1b.tiff 2b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 01b.tiff 02b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 001b.tiff 002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 0001b.tiff 0002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 00001b.tiff 00002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5' | cut -d "." -f2 | tr -d "/").tiff & 
#wait for jobs to end
    wait < <(jobs -p)
#remove unwanted files
rm 00001.tiff 00002.tiff 0001.tiff 0002.tiff 001.tiff 002.tiff 01.tiff 02.tiff 1.tiff 2.tiff
rm 00001b.tiff 00002b.tiff 0001b.tiff 0002b.tiff 001b.tiff 002b.tiff 01b.tiff 02b.tiff 1b.tiff 2b.tiff
rm $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1')
fi
done

#check for audio
if ! [ x"$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) 2>&1 | grep Audio)" = x ]
then 
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) -vn -acodec copy $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1 | cut -d "." -f1).wav
wav=$(printf "%s\n" -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1 | cut -d "." -f1).wav)
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi
#tif folders
if ls *.wav
then
wav=$(printf "%s\n" -i $(ls *.wav | awk 'FNR == 1'))
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi

#output to prores
echo "" >> /tmp/HDRMOV_LOGS/HDR_script1_LOG.txt
echo "##################ffmpeg output script1#####################" >> /tmp/HDRMOV_LOGS/HDR_script1_LOG.txt
#check for tif folders
if ! grep './' /tmp/HDRMOVab
then
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(/usr/local/bin/exiftool $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) | grep 'Video Frame Rate' | cut -d ":" -f2) -i %06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le ../HDR_$(cat /tmp/HDRMOVab | head -1 | cut -d "." -f1).mov 2>> /tmp/HDRMOV_LOGS/HDR_script1_LOG.txt
#remove tiff files and HDR mov when done
#check for matching MLV or else do not remove
check="$(cat /tmp/HDRMOVab | head -1 | cut -d "." -f1)".MLV
if grep "$check" <<< $(cat /tmp/mlvapp_path/file_names.txt)
then
rm -r ../$(cat /tmp/HDRMOVab | head -1 | cut -d "." -f1)
else
mv *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} ../
rm -r ../$(cat /tmp/HDRMOVab | head -1 | cut -d "." -f1)
fi
else
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(cat fps) -i "$(cat /tmp/HDRMOVab | head -1 | cut -d '/' -f2 | cut -d "." -f1)"_%06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le ../HDR_$(cat /tmp/HDRMOVab | head -1 | cut -d '/' -f2 | cut -d "." -f1).mov 2>> /tmp/HDRMOV_LOGS/HDR_script1_LOG.txt
#remove tiff files when done
#check for matching MLV or else do not remove
check="$(cat /tmp/HDRMOVab | head -1 | cut -d '/' -f2 | cut -d "." -f1)".MLV
if grep "$check" <<< $(cat /tmp/mlvapp_path/file_names.txt)
then
rm -r ../$(cat /tmp/HDRMOVab | head -1 | cut -d '/' -f2 | cut -d "." -f1)
fi
fi
#let´s go back 
cd -
if ls /tmp/KILLMOV 
then 
rm /tmp/HDRMOVab
fi
echo "$(tail -n +2 /tmp/HDRMOVab)" > /tmp/HDRMOVab
done
rm /tmp/HDRMOVab
rm /tmp/HDR_script1.command
EOF
fi


if grep 'MOV\|mov\|mp4\|MP4\|mkv\|MKV\|avi\|AVI\|./' /tmp/HDRMOVac
then
cat <<'EOF' > /tmp/HDR_script2.command
#!/bin/bash
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"

#log
echo "##################HDR_script2.command#####################" >> /tmp/HDRMOV_LOGS/HDR_script2_LOG.txt
#run the log file

while grep 'MOV\|mov\|mp4\|MP4\|mkv\|MKV\|avi\|AVI\|./' /tmp/HDRMOVac; do
#build a temp folder only if it´s not a folder
if ! grep './' /tmp/HDRMOVac
then
mkdir -p $(cat /tmp/HDRMOVac | head -1 | cut -d "." -f1)
mv $(cat /tmp/HDRMOVac | head -1) $(cat /tmp/HDRMOVac | head -1 | cut -d "." -f1)
cd "$(cat /tmp/HDRMOVac | head -1 | cut -d "." -f1)"
#spit out tif files
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(cat /tmp/HDRMOVac | head -1) -pix_fmt rgb24 %06d.tif
else
#seems we have tif folders
cd "$(cat /tmp/HDRMOVac | head -1 | cut -d '/' -f2)"
fi

#crop and rescale is needed is needed after aligning. Will take place in #output cropped and aligned images section
#check for tif folders
if ! grep './' /tmp/HDRMOVac
then
cr_W=$(echo $(/usr/local/bin/exiftool $(cat /tmp/HDRMOVac | head -1) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f1 ))
cr_H=$(echo $(/usr/local/bin/exiftool $(cat /tmp/HDRMOVac | head -1) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f2 ))
else
cr_W=$(echo $(/usr/local/bin/exiftool $(echo *_000000.tif) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f1 ))
cr_H=$(echo $(/usr/local/bin/exiftool $(echo *_000000.tif) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f2 ))
fi
cr_Ws=$(echo $cr_W*0.98 | bc -l | cut -d "." -f1)
cr_Hs=$(echo $cr_H*0.98 | bc -l | cut -d "." -f1)
crp_fix=$(echo crop=$cr_Ws:$cr_Hs,scale=$cr_W:$cr_H)

#First script combines enfused and aligned tif files then exports it to a prores mov file

while grep -E "tif" <<< $(find . -maxdepth 1 -iname '*.tif')
do

#align images and rename
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') && mv aligned.tif0000.tif 1.tiff && mv aligned.tif0001.tif 2.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned2.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') && mv aligned2.tif0000.tif 01.tiff && mv aligned2.tif0001.tif 02.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned3.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') && mv aligned3.tif0000.tif 001.tiff && mv aligned3.tif0001.tif 002.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned4.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') && mv aligned4.tif0000.tif 0001.tiff && mv aligned4.tif0001.tif 0002.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned5.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') && mv aligned5.tif0000.tif 00001.tiff && mv aligned5.tif0001.tif 00002.tiff &

#wait for jobs to end
    wait < <(jobs -p)

#if killing process
if ! ls /tmp/KILLMOV 
then 
#enfuse workflow. Needs crop to care for align processing. Roundtrip because of enfuse interpolation causing flicker otherwise
#output cropped and aligned images
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 00001.tiff -pix_fmt rgb24 -vf $crp_fix 00001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 00002.tiff -pix_fmt rgb24 -vf $crp_fix 00002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 0001.tiff -pix_fmt rgb24 -vf $crp_fix 0001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 0002.tiff -pix_fmt rgb24 -vf $crp_fix 0002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 001.tiff -pix_fmt rgb24 -vf $crp_fix 001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 002.tiff -pix_fmt rgb24 -vf $crp_fix 002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 01.tiff -pix_fmt rgb24 -vf $crp_fix 01b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 02.tiff -pix_fmt rgb24 -vf $crp_fix 02b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 1.tiff -pix_fmt rgb24 -vf $crp_fix 1b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 2.tiff -pix_fmt rgb24 -vf $crp_fix 2b.tiff &
#wait for jobs to end
    wait < <(jobs -p)

/Applications/Hugin/tools_mac/enfuse 1b.tiff 2b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 01b.tiff 02b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 001b.tiff 002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 0001b.tiff 0002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 00001b.tiff 00002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5' | cut -d "." -f2 | tr -d "/").tiff & 
#wait for jobs to end
    wait < <(jobs -p)
#remove unwanted files
rm 00001.tiff 00002.tiff 0001.tiff 0002.tiff 001.tiff 002.tiff 01.tiff 02.tiff 1.tiff 2.tiff
rm 00001b.tiff 00002b.tiff 0001b.tiff 0002b.tiff 001b.tiff 002b.tiff 01b.tiff 02b.tiff 1b.tiff 2b.tiff
rm $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1')
fi
done

#check for audio
if ! [ x"$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) 2>&1 | grep Audio)" = x ]
then 
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) -vn -acodec copy $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1 | cut -d "." -f1).wav
wav=$(printf "%s\n" -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1 | cut -d "." -f1).wav)
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi
#tif folders
if ls *.wav
then
wav=$(printf "%s\n" -i $(ls *.wav | awk 'FNR == 1'))
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi

#output to prores
echo "" >> /tmp/HDRMOV_LOGS/HDR_script2_LOG.txt
echo "##################ffmpeg output script2#####################" >> /tmp/HDRMOV_LOGS/HDR_script2_LOG.txt
#check for tif folders
if ! grep './' /tmp/HDRMOVac
then
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(/usr/local/bin/exiftool $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) | grep 'Video Frame Rate' | cut -d ":" -f2) -i %06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le ../HDR_$(cat /tmp/HDRMOVac | head -1 | cut -d "." -f1).mov 2>> /tmp/HDRMOV_LOGS/HDR_script2_LOG.txt
#remove tiff files and HDR mov when done
#check for matching MLV or else do not remove
check="$(cat /tmp/HDRMOVac | head -1 | cut -d "." -f1)".MLV
if grep "$check" <<< $(cat /tmp/mlvapp_path/file_names.txt)
then
rm -r ../$(cat /tmp/HDRMOVac | head -1 | cut -d "." -f1)
else
mv *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} ../
rm -r ../$(cat /tmp/HDRMOVac | head -1 | cut -d "." -f1)
fi
else
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(cat fps) -i "$(cat /tmp/HDRMOVac | head -1 | cut -d '/' -f2 | cut -d "." -f1)"_%06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le ../HDR_$(cat /tmp/HDRMOVac | head -1 | cut -d '/' -f2 | cut -d "." -f1).mov 2>> /tmp/HDRMOV_LOGS/HDR_script2_LOG.txt
#remove tiff files when done
#check for matching MLV or else do not remove
check="$(cat /tmp/HDRMOVac | head -1 | cut -d '/' -f2 | cut -d "." -f1)".MLV
if grep "$check" <<< $(cat /tmp/mlvapp_path/file_names.txt)
then
rm -r ../$(cat /tmp/HDRMOVac | head -1 | cut -d '/' -f2 | cut -d "." -f1)
fi
fi
#let´s go back 
cd -
if ls /tmp/KILLMOV 
then 
rm /tmp/HDRMOVac
fi
echo "$(tail -n +2 /tmp/HDRMOVac)" > /tmp/HDRMOVac
done
rm /tmp/HDRMOVac
rm /tmp/HDR_script2.command
EOF
fi


if grep 'MOV\|mov\|mp4\|MP4\|mkv\|MKV\|avi\|AVI\|./' /tmp/HDRMOVad
then
cat <<'EOF' > /tmp/HDR_script3.command
#!/bin/bash
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"

#log
echo "##################HDR_script3.command#####################" >> /tmp/HDRMOV_LOGS/HDR_script3_LOG.txt
#run the log file

while grep 'MOV\|mov\|mp4\|MP4\|mkv\|MKV\|avi\|AVI\|./' /tmp/HDRMOVad; do
#build a temp folder only if it´s not a folder
if ! grep './' /tmp/HDRMOVad
then
mkdir -p $(cat /tmp/HDRMOVad | head -1 | cut -d "." -f1)
mv $(cat /tmp/HDRMOVad | head -1) $(cat /tmp/HDRMOVad | head -1 | cut -d "." -f1)
cd "$(cat /tmp/HDRMOVad | head -1 | cut -d "." -f1)"
#spit out tif files
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(cat /tmp/HDRMOVad | head -1) -pix_fmt rgb24 %06d.tif
else
#seems we have tif folders
cd "$(cat /tmp/HDRMOVad | head -1 | cut -d '/' -f2)"
fi

#crop and rescale is needed is needed after aligning. Will take place in #output cropped and aligned images section
#check for tif folders
if ! grep './' /tmp/HDRMOVad
then
cr_W=$(echo $(/usr/local/bin/exiftool $(cat /tmp/HDRMOVad | head -1) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f1 ))
cr_H=$(echo $(/usr/local/bin/exiftool $(cat /tmp/HDRMOVad | head -1) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f2 ))
else
cr_W=$(echo $(/usr/local/bin/exiftool $(echo *_000000.tif) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f1 ))
cr_H=$(echo $(/usr/local/bin/exiftool $(echo *_000000.tif) | awk '/Image Size/ { print $4,$5; exit }' | cut -d ":" -f2 | cut -d "x" -f2 ))
fi
cr_Ws=$(echo $cr_W*0.98 | bc -l | cut -d "." -f1)
cr_Hs=$(echo $cr_H*0.98 | bc -l | cut -d "." -f1)
crp_fix=$(echo crop=$cr_Ws:$cr_Hs,scale=$cr_W:$cr_H)

#First script combines enfused and aligned tif files then exports it to a prores mov file

while grep -E "tif" <<< $(find . -maxdepth 1 -iname '*.tif')
do

#align images and rename
/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') && mv aligned.tif0000.tif 1.tiff && mv aligned.tif0001.tif 2.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned2.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') && mv aligned2.tif0000.tif 01.tiff && mv aligned2.tif0001.tif 02.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned3.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') && mv aligned3.tif0000.tif 001.tiff && mv aligned3.tif0001.tif 002.tiff &

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned4.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') && mv aligned4.tif0000.tif 0001.tiff && mv aligned4.tif0001.tif 0002.tiff & 

/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack -a aligned5.tif $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 6') && mv aligned5.tif0000.tif 00001.tiff && mv aligned5.tif0001.tif 00002.tiff &

#wait for jobs to end
    wait < <(jobs -p)

#if killing process
if ! ls /tmp/KILLMOV 
then 

#enfuse workflow. Needs crop to care for align processing. Roundtrip because of enfuse interpolation causing flicker otherwise
#output cropped and aligned images
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 00001.tiff -pix_fmt rgb24 -vf $crp_fix 00001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 00002.tiff -pix_fmt rgb24 -vf $crp_fix 00002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 0001.tiff -pix_fmt rgb24 -vf $crp_fix 0001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 0002.tiff -pix_fmt rgb24 -vf $crp_fix 0002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 001.tiff -pix_fmt rgb24 -vf $crp_fix 001b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 002.tiff -pix_fmt rgb24 -vf $crp_fix 002b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 01.tiff -pix_fmt rgb24 -vf $crp_fix 01b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 02.tiff -pix_fmt rgb24 -vf $crp_fix 02b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 1.tiff -pix_fmt rgb24 -vf $crp_fix 1b.tiff &
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i 2.tiff -pix_fmt rgb24 -vf $crp_fix 2b.tiff &
#wait for jobs to end
    wait < <(jobs -p)

/Applications/Hugin/tools_mac/enfuse 1b.tiff 2b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 01b.tiff 02b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 001b.tiff 002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 0001b.tiff 0002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4' | cut -d "." -f2 | tr -d "/").tiff &
/Applications/Hugin/tools_mac/enfuse 00001b.tiff 00002b.tiff -o $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5' | cut -d "." -f2 | tr -d "/").tiff & 
#wait for jobs to end
    wait < <(jobs -p)
#remove unwanted files
rm 00001.tiff 00002.tiff 0001.tiff 0002.tiff 001.tiff 002.tiff 01.tiff 02.tiff 1.tiff 2.tiff
rm 00001b.tiff 00002b.tiff 0001b.tiff 0002b.tiff 001b.tiff 002b.tiff 01b.tiff 02b.tiff 1b.tiff 2b.tiff
rm $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 5') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 4') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 3') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 2') $(find -s . -maxdepth 1 -iname '*.tif' | awk 'FNR == 1')
fi
done

#check for audio
if ! [ x"$("$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) 2>&1 | grep Audio)" = x ]
then 
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) -vn -acodec copy $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1 | cut -d "." -f1).wav
wav=$(printf "%s\n" -i $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1 | cut -d "." -f1).wav)
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi
#tif folders
if ls *.wav
then
wav=$(printf "%s\n" -i $(ls *.wav | awk 'FNR == 1'))
acodec=$(printf "%s\n" -c:v copy -c:a aac)
fi

#output to prores
echo "" >> /tmp/HDRMOV_LOGS/HDR_script3_LOG.txt
echo "##################ffmpeg output script3#####################" >> /tmp/HDRMOV_LOGS/HDR_script3_LOG.txt
#check for tif folders
if ! grep './' /tmp/HDRMOVad
then
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(/usr/local/bin/exiftool $(ls *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} | grep -v 'HDR_' | head -1) | grep 'Video Frame Rate' | cut -d ":" -f2) -i %06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le ../HDR_$(cat /tmp/HDRMOVad | head -1 | cut -d "." -f1).mov 2>> /tmp/HDRMOV_LOGS/HDR_script3_LOG.txt
#remove tiff files and HDR mov when done
#check for matching MLV or else do not remove
check="$(cat /tmp/HDRMOVad | head -1 | cut -d "." -f1)".MLV
if grep "$check" <<< $(cat /tmp/mlvapp_path/file_names.txt)
then
rm -r ../$(cat /tmp/HDRMOVad | head -1 | cut -d "." -f1)
else
mv *.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} ../
rm -r ../$(cat /tmp/HDRMOVad | head -1 | cut -d "." -f1)
fi
else
"$(cat /tmp/mlvapp_path/app_path.txt)"/ffmpeg $wav -r $(cat fps) -i "$(cat /tmp/HDRMOVad | head -1 | cut -d '/' -f2 | cut -d "." -f1)"_%06d.tiff $acodec -vcodec prores -pix_fmt yuv422p10le ../HDR_$(cat /tmp/HDRMOVad | head -1 | cut -d '/' -f2 | cut -d "." -f1).mov 2>> /tmp/HDRMOV_LOGS/HDR_script3_LOG.txt
#remove tiff files when done
#check for matching MLV or else do not remove
check="$(cat /tmp/HDRMOVad | head -1 | cut -d '/' -f2 | cut -d "." -f1)".MLV
if grep "$check" <<< $(cat /tmp/mlvapp_path/file_names.txt)
then
rm -r ../$(cat /tmp/HDRMOVad | head -1 | cut -d '/' -f2 | cut -d "." -f1)
fi
fi
#let´s go back 
cd -
if ls /tmp/KILLMOV 
then 
rm /tmp/HDRMOVad
fi
echo "$(tail -n +2 /tmp/HDRMOVad)" > /tmp/HDRMOVad
done
rm /tmp/HDRMOVad
rm /tmp/HDR_script3.command
EOF
fi

#fps command
cat <<'EOF' > /tmp/fps.command
#!/bin/bash
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"

echo "##################fps.command#####################" >> /tmp/HDRMOV_LOGS/HDR_fps_LOG.txt
#run the log file

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
. /tmp/fps.command
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
. /tmp/fps.command
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
. /tmp/fps.command
;;
esac
fi

chmod u=rwx /tmp/HDR_script.command
chmod u=rwx /tmp/HDR_script1.command
chmod u=rwx /tmp/HDR_script2.command
chmod u=rwx /tmp/HDR_script3.command

#add log structure
rm -r /tmp/HDRMOV_LOGS
mkdir /tmp/HDRMOV_LOGS

sleep 1 && . /tmp/HDR_script.command | tee -a "$(cat /tmp/mlvapp_path/output_folder.txt)"/HDRMOV_LOG.txt &
sleep 1 && . /tmp/HDR_script1.command | tee -a /tmp/HDRMOV_LOGS/HDR_script1_LOG.txt &
sleep 1 && . /tmp/HDR_script2.command | tee -a /tmp/HDRMOV_LOGS/HDR_script2_LOG.txt &
sleep 1 && . /tmp/HDR_script3.command | tee -a /tmp/HDRMOV_LOGS/HDR_script3_LOG.txt &

sleep 1 && rm /tmp/fps.command & echo -n -e "\033]0;fps\007" && osascript -e 'tell application "Terminal" to close (every window whose name contains "fps")' & exit

EOF

cat <<'EOF' > /tmp/progress_bar.command

#!/bin/bash
cd "$(cat /tmp/mlvapp_path/output_folder.txt)"

patio="$PWD"

#HDR_MOV bash
printf '\e[8;9;24t'
printf '\e[3;955;0t'

while :
do 

clear
cat<<EOF1
    ------------
    $(tput setaf 0)$(tput bold)HDR_MOV bash$(tput sgr0)
    ------------
 $(tput bold)$(tput setaf 1)(C) CANCEL$(tput sgr0)
 $(tput bold)$(tput setaf 1)(e) Close this window$(tput sgr0)

Selection number:
EOF1
    read -n1
    case "$REPLY" in
    "C") 
echo "" >> HDRMOV_LOG.txt
echo "##################progress_bar.command#####################" >> HDRMOV_LOG.txt
#run the log file

echo "" >> HDRMOV_LOG.txt
echo "ALL PROCESSES ARE CANCELLED!" >> HDRMOV_LOG.txt
echo "" >> HDRMOV_LOG.txt
echo > /tmp/KILLMOV
killall sleep
killall align_image_stack
mv "$patio"/"$(cat /tmp/HDRMOVaa | head -1 | cut -d "." -f1)"/*.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} "$patio"
mv "$patio"/"$(cat /tmp/HDRMOVab | head -1 | cut -d "." -f1)"/*.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} "$patio"
mv "$patio"/"$(cat /tmp/HDRMOVac | head -1 | cut -d "." -f1)"/*.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} "$patio"
mv "$patio"/"$(cat /tmp/HDRMOVad | head -1 | cut -d "." -f1)"/*.{MOV,mov,mp4,MP4,mkv,MKV,avi,AVI} "$patio"

sleep 2 && rm /tmp/progress_bar.command &
sleep 2 && rm /tmp/HDR_script*.command &
rm /tmp/HDRMOV*
rm /tmp/HDR_script*.command
rm /tmp/KILLMOV
echo echo "$(date)" >> HDRMOV_LOG.txt
osascript -e 'tell application "Terminal" to close first window' & exit
;;

    "e") 
osascript -e 'tell application "Terminal" to close first window' & exit
;;

    "Q")  echo "case sensitive!!"   ;;
     * )  echo "invalid option"     ;;
    esac 
done
EOF

#open fps command and progress_bar.command
chmod u=rwx /tmp/fps.command
chmod u=rwx /tmp/progress_bar.command
chmod u=rwx "$(cat /tmp/mlvapp_path/app_path.txt | tr -d '"' )"/HDR_MOV.command

#check what´s missing
[ ! -f "/usr/local/bin/brew" ] && brew=$(echo brew missing)
[ ! -f "/Applications/Hugin/Hugin.app/Contents/MacOS/align_image_stack" ] && hugin=$(echo hugin missing)
[ ! -f "/usr/local/bin/exiftool" ] && exiftool=$(echo exiftool missing)

if [ x$brew$hugin$exiftool = x ]
then
sleep 0.2 && . /tmp/fps.command
else
sleep 0.2 && open /tmp/fps.command
fi

#kill ongoing command
echo -n -e "\033]0;HDR_script\007" && osascript -e 'tell application "Terminal" to close (every window whose name contains "HDR_script")' & exit



