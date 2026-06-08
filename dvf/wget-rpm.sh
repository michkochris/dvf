#!/bin/bash
# wget-rpm.sh - Vamped RPM downloader for dvf testing

# Robust Fedora version detection
if [ -f /etc/os-release ]; then
    FEDORA_VER=$(grep -oP '(?<=VERSION_ID=)\d+' /etc/os-release)
    if [ -z "$FEDORA_VER" ]; then
        FEDORA_VER=$(grep -oP '(?<=VERSION_ID=)\w+' /etc/os-release)
    fi
fi

# Manual override for Fedora 44 which is currently in the 'rawhide' branch
if [[ "$FEDORA_VER" == "44" ]] || [[ "$FEDORA_VER" == "rawhide" ]]; then
    # Rawhide/Development versions often use 'rawhide' in the URL path instead of a number
    BASE_URL="https://download.fedoraproject.org/pub/fedora/linux/development/rawhide/Everything/x86_64/os/Packages"
    FEDORA_VER="44"
elif [ "$FEDORA_VER" -ge 42 ]; then
    BASE_URL="https://download.fedoraproject.org/pub/fedora/linux/development/${FEDORA_VER}/Everything/x86_64/os/Packages"
else
    BASE_URL="https://download.fedoraproject.org/pub/fedora/linux/releases/${FEDORA_VER}/Everything/x86_64/os/Packages"
fi

mkdir -p test_rpms
cd test_rpms || exit

echo "-------------------------------------------------------"
echo "  DVF Test Suite: Fetching RPMs (Fedora $FEDORA_VER)"
echo "-------------------------------------------------------"

if [ -n "$1" ]; then
    SEARCH_TERM="$1"
    FIRST_LETTER=$(echo "${SEARCH_TERM:0:1}" | tr '[:upper:]' '[:lower:]')

    echo " [?] Searching for '$SEARCH_TERM' in $BASE_URL/$FIRST_LETTER/ ..."

    # Fetch directory listing and find the first match for x86_64
    LISTING=$(curl -skL "${BASE_URL}/${FIRST_LETTER}/")

    if [ -z "$LISTING" ]; then
        echo " [!] Error: Could not fetch directory listing from $BASE_URL/$FIRST_LETTER/"
        exit 1
    fi

    # Try to find the match. We look for the filename in the href attribute.
    MATCH=$(echo "$LISTING" | grep -oP 'href="\K'${SEARCH_TERM}'[^"]+x86_64\.rpm' | head -n 1)

    if [ -n "$MATCH" ]; then
        PACKAGES=("${FIRST_LETTER}/${MATCH}")
    else
        echo " [!] Error: Could not find any RPM matching '$SEARCH_TERM' in Fedora $FEDORA_VER mirrors."
        echo " [i] Listing checked: ${BASE_URL}/${FIRST_LETTER}/"
        exit 1
    fi
else
    # Minimal default set
    PACKAGES=(
        "h/hello-2.10-10.fc${FEDORA_VER}.x86_64.rpm"
    )
    echo " [i] No package specified. Downloading default test set..."
fi

for pkg in "${PACKAGES[@]}"; do
    filename=$(basename "$pkg")
    if [ -f "$filename" ]; then
        echo " [+] Skipping $filename (already exists)"
    else
        echo " [>] Fetching $filename..."
        # Note: Removing -L as it is not a valid wget option (it's a curl option)
        # wget follows redirects by default.
        wget -q -c "${BASE_URL}/${pkg}"
        if [ $? -eq 0 ]; then
            echo " [OK] Downloaded $filename"
        else
            echo " [!] Failed to download $filename"
        fi
    fi
done

echo "-------------------------------------------------------"
echo " Done! Files are in $(pwd)"
echo "-------------------------------------------------------"
