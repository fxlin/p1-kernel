pptobjects := $(patsubst %.pptx,%.pdf,$(wildcard *.pptx))

all : $(docobjects) $(pptobjects)

clean:
	rm -rf $(docobjects) $(pptobjects)

%.pdf.uncrop : %.pptx
	/cygdrive/c/Windows/SysWOW64/cscript.exe ppt.vbs `cygpath -aw $^` `cygpath -aw $@`

%.pdf: %.pdf.uncrop
	pdfcrop $^ $@