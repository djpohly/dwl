#!/bin/bash

# wiki directory was initially formatted via: sed -i '/https:\/\/github\.com\/[^/]\+\/[^/]\+\/compare\/[^/]\+\.patch/s/$/ [❔]/'

dwlSrcDirectory="$1"
wikiDirectory="$2"
patchLinkPattern="https://github\.com/[^/]+/[^/]+/compare/[^/]+\.patch"
emojiReplacePattern="\(\[[❔⚠️❌✅]\]\)"

for file in "$wikiDirectory"/*.md; do

    bFileName=$(basename "$file")
    if ! [ -f "$file" ] ||
        [ "$bFileName" == "Patches.md" ] || \
        [ "$bFileName" == "Screenshots.md" ] || \
        [ "$bFileName" == "Home.md" ] || \
        [ "$bFileName" == "_Sidebar.md" ]; then
        echo "$file is invalid, skipping"
        continue
    fi
    if ! grep -q "Download" "$file"; then
        echo "no download heading found in $file, skipping..."
        continue
    fi

    tempFile="$file.tmp"
    touch "$tempFile"
    while IFS= read -r line || [ -n "$line" ]; do
        if ! [[ $line =~ $patchLinkPattern ]]; then # not a download link
            echo "$line" >> "$tempFile"
            continue
        fi

        extractedURL=${BASH_REMATCH[0]}
        response=$(curl -s -w "%{http_code}" -o - "$extractedURL")

        http_status_code="${response: -3}"
        patchContent="${response:0:-3}"

        if [ "$http_status_code" -ne 200 ] || [ -z "$patchContent" ]; then
            echo "[⚠️] -- $extractedURL"
            echo "$line" | sed "s/$emojiReplacePattern/[⚠️]/1" >> "$tempFile"
            continue
        fi

        git -C "$dwlSrcDirectory" apply --check <<< "$patchContent" > /dev/null 2>&1
        patchApplicationExitCode=$?

        if [ $patchApplicationExitCode -eq 0 ]; then
            echo "[✅] -- $extractedURL"
            echo "$line" | sed "s/$emojiReplacePattern/[✅]/1" >> "$tempFile"
        else
            echo "[❌] -- $extractedURL"
            echo "$line" | sed "s/$emojiReplacePattern/[❌]/1" >> "$tempFile"
        fi
    done < "$file"

    mv "$tempFile" "$file"
done
