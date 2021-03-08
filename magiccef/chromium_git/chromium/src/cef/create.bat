set GN_DEFINES=is_component_build=true
# Use vs2017 or vs2019 as appropriate.
set GN_ARGUMENTS=--ide=vs2017 --sln=cef --filters=//cef/*
call cef_create_projects.bat