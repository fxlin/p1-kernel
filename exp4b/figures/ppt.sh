#!/bin/bash
# convert pptx to pdf using libreoffice

# must lift convert policy
# https://askubuntu.com/questions/1081895/trouble-with-batch-conversion-of-png-to-pdf-using-convert

fbname=$(basename "$1" .pptx)

# optional: auto pptx->pdf. not perfect
#libreoffice --headless --invisible --convert-to pdf $1

pdfcrop ${fbname}.pdf ${fbname}-cropped.pdf
convert -density 300 ${fbname}-cropped.pdf ${fbname}.png



#pdftk ${fbname}.pdf cat 1 output ${fbname}.pdf.page1