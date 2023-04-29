#!/bin/bash
cd $(dirname $0)

ansiweather -l "${USER_CITY_CODE},${USER_CC}" -u "$USER_UNITS" -s false -a false
