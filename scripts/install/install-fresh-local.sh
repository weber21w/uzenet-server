#!/bin/bash
cd $(dirname $0)

DOMAIN_NAME="$1"
if [[ -z "$DOMAIN_NAME" ]]; then DOMAIN_NAME='uzenet.us'; fi #TODO REMOVE THIS
UNS_GITHUB_URL="$2"
if [[ -z "$UNS_GITHUB_URL" ]]; then UNS_GITHUB_URL='https://github.com/weber21w/uzenet-server/archive/refs/heads/main.zip'; fi
CUZEBOX_GITHUB_URL="$3" #need ESP8266 version, with headless mode
if [[ -z "$CUZEBOX_GITHUB_URL" ]]; then CUZEBOX_GITHUB_URL='https://github.com/weber21w/cuzebox-esp8266/archive/refs/heads/main.zip'; fi
ENABLE_SHELLINABOX="$4"
if [[ -z "$DOMAIN_NAME" ]]; then echo "ERROR requires [DOMAIN-NAME] [OPT-UNS-GITHUB-ZIP-URL] [OPT-CUZEBOX-GITHUB-ZIP-URL]"; exit 1; fi

CUSTOM_SHELLINABOX_PORT="4200"

cd ~

########Install Dependencies########
RES=$(dpkg -s unzip)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [unzip]**"; sudo echo ""; sudo apt install -y unzip; fi
RES=$(dpkg -s wget)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [wget]**"; sudo echo ""; sudo apt install -y wget; fi
RES=$(dpkg -s make)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [make]**"; sudo echo ""; sudo apt install -y make; fi
RES=$(dpkg -s make)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [gcc]**"; sudo echo ""; sudo apt install -y gcc; fi
RES=$(dpkg -s libsdl2-dev)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [libsdl2-dev]**"; sudo echo ""; sudo apt install -y libsdl2-dev; fi
RES=$(dpkg -s sshpass)
if [[ ! "$RES" == *'installed'* ]]; then echo "**NEED SUDO TO INSTALL [sshpass]**"; sudo echo ""; sudo apt install -y sshpass; fi



########Install UNS########
wget -O uns-latest.zip ${UNS_GITHUB_URL}
unzip -o uns-latest.zip #overwrite existing
rm uns-latest.zip
mv *uze* uns #make standard name
cd uns
make

#create a systemd service at boot
echo "**NEED SUDO TO INSTALL [uzenet-server]**"; sudo echo "";
sudo echo '
[Unit]
Description=Uzenet Server

[Service]
ExecStart=/home/uzenet/uns/uns

[Install]
WantedBy=multi-user.target
' | sudo tee '/etc/systemd/system/uzenet-server.service'

systemctl enable uzenet-server #enable uzenet server to run at boot
sudo systemctl start uzenet-server #start immediately



########Install CUzeBox########
rm -r *cuze* #get rid of any old stuff

wget -O cuzebox-latest.zip ${CUZEBOX_GITHUB_URL}
unzip -o cuzebox-latest.zip #overwrite existing

rm cuzebox-latest.zip
mv *cuze* cuzebox #make standard name
cd cuzebox

#make headless version for bots and other purposes
HEADLESS_CFG=$(cat 'Make_config.mk' | sed 's/FLAG_HEADLESS=0/FLAG_HEADLESS=1/g') #modify headless config
echo "$HEADLESS_CFG" > 'Make_config.mk'
make
cd ~


########Install Caddy lightweight static webserver(with automatic SSL)########
RES=$(dpkg -s caddy)
if [[ "$RES" == *'installed'* ]]; then sudo systemctl stop caddy; sudo apt remove -y caddy; fi

echo "**NEED SUDO TO INSTALL [caddy]**"; sudo echo "";
sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update -y
sudo apt install -y caddy

DOMAIN_NAME="$1"
if [[ -z "$DOMAIN_NAME" ]]; then echo "ERROR must specify domain name, ie \"uzenet.us\""; exit 1; fi


CFG_DATA=$(cat '/etc/caddy/Caddyfile') #get cfg file data
CFG_DATA=$(echo "$CFG_DATA" | sed "s/:80 {/${DOMAIN_NAME} {/g") #set our domain(so SSL will work)
CFG_DATA=$(echo "$CFG_DATA" | sed "s/root \* \/usr\/share\/caddy/root \* \/var\/www\/html/g")
echo "$CFG_DATA" | sudo tee '/etc/caddy/Caddyfile'

echo "
${DOMAIN_NAME} {
root * /home/uzenet/uns/html
file_server
}
" | sudo tee /etc/caddy/Caddyfile


sudo systemctl reload caddy #it may take 3-4 days for the SSL certificate to right itself?



########Install Ansiweather########
sudo echo ""
sudo apt install -y ansiweather



#Optionally Install Shellinabox########
if [[ ! -z "$ENABLE_SHELLINABOX" ]]
then
	sudo apt install -y openssl shellinabox
	if [[ "$CUSTOM_SHELLINABOX_PORT" -ne "4200" ]]
	then
		sudo sed -i "/SHELLINABOX_PORT=4200/c\SHELLINABOX_PORT=$NEW_PORT" /etc/default/shellinabox
	fi
fi

sudo service shellinabox stop
sudo service shellinabox start
