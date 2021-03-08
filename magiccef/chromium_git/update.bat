set GN_DEFINES=is_component_build=true
# Use vs2017 or vs2019 as appropriate.
set GN_ARGUMENTS=--ide=vs2017 --sln=cef --filters=//cef/*
python ../automate/automate-git.py --download-dir=d:/github/cef/3809/chromium_git --depot-tools-dir=d:/github/cef/3809/depot_tools --branch=3809 --no-distrib --no-build