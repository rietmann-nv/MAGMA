###
#
#  @file installCUDA.cmake
#
#  @project MORSE
#  MORSE is a software package provided by:
#     Inria Bordeaux - Sud-Ouest,
#     Univ. of Tennessee,
#     Univ. of California Berkeley,
#     Univ. of Colorado Denver.
#
#  @version 0.1.0
#  @author Cedric Castagnede
#  @date 13-07-2012
#
###

CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

MACRO(INSTALL_CUDA _MODE)

    MESSAGE(FATAL_ERROR "MORSE build system did not find CUDA and cannot install it.")

ENDMACRO(INSTALL_CUDA)

##
## @end file installCUDA.cmake
##
