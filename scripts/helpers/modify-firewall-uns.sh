cd $(dirname $0)

NEW_PORT="$1" #default is 4200
sudo apt install -y openssl shellinabox
if [[ ! -z "$NEW_PORT" ]]
then
sudo sed -i "/SHELLINABOX_PORT=4200/c\SHELLINABOX_PORT=$NEW_PORT" /etc/default/shellinabox
fi
sudo service shellinabox stop
sudo service shellinabox start

