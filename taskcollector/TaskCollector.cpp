#include "FreeImage.h"
#include <algorithm>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <list>
#include <memory>
#include <queue>
#include <iostream>

using bitmap_t = std::shared_ptr<FIBITMAP>;

/** Generic image loader
@param lpszPathName Pointer to the full file name
@param flag Optional load flag constant
@return Returns the loaded dib if successful, returns NULL otherwise
*/
bitmap_t GenericLoader(const char* lpszPathName, int flag) {
	FREE_IMAGE_FORMAT fif = FIF_UNKNOWN;

	// check the file signature and deduce its format
	// (the second argument is currently not used by FreeImage)
	fif = FreeImage_GetFileType(lpszPathName, 0);
	if (fif == FIF_UNKNOWN) {
		// no signature ?
		// try to guess the file format from the file extension
		fif = FreeImage_GetFIFFromFilename(lpszPathName);
	}
	// check that the plugin has reading capabilities ...
	if ((fif != FIF_UNKNOWN) && FreeImage_FIFSupportsReading(fif)) {
		// ok, let's load the file
		auto dib = std::make_shared<FIBITMAP>(*FreeImage_Load(fif, lpszPathName, flag));
		// unless a bad file format, we are done !
		return dib;
	}
	return NULL;
}

/** Generic image writer
@param dib Pointer to the dib to be saved
@param lpszPathName Pointer to the full file name
@param flag Optional save flag constant
@return Returns true if successful, returns false otherwise
*/
bool GenericWriter(bitmap_t dib, const char* lpszPathName, int flag) {
	FREE_IMAGE_FORMAT fif = FIF_UNKNOWN;
	BOOL bSuccess = FALSE;
	if (dib) {
		// try to guess the file format from the file extension
		fif = FreeImage_GetFIFFromFilename(lpszPathName);
		if (fif != FIF_UNKNOWN) {
			// check that the plugin has sufficient writing and export capabilities ...
			if (FreeImage_FIFSupportsWriting(fif) && FreeImage_FIFSupportsExportType(fif, FreeImage_GetImageType(dib.get()))) {
				// ok, we can save the file
				bSuccess = FreeImage_Save(fif, dib.get(), lpszPathName, flag);
				// unless an abnormal bug, we are done !
			}
			else {
				std::cout << "Can't save file" << lpszPathName << std::endl;
			}
		}
	}
	return (bSuccess == TRUE);
}

// ----------------------------------------------------------

/**
FreeImage error handler
@param fif Format / Plugin responsible for the error
@param message Error message
*/
void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message) {
	printf("\n*** ");
	if (fif != FIF_UNKNOWN) {
		printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
	}
	printf(message);
	printf(" ***\n");
}

// ----------------------------------------------------------

class TaskCollector {

protected:
	std::vector<bitmap_t> chunks;
	std::vector<bitmap_t> alphaChunks;

public:
	TaskCollector() = default;
	TaskCollector(const TaskCollector&) = default;
	//TaskCollector(TaskCollector &&) = default;	// for some reason visual think it's incorrect constructor
	~TaskCollector()
	{
		std::for_each(chunks.begin(), chunks.end(), [](bitmap_t el){ FreeImage_Unload(el.get()); });
		std::for_each(alphaChunks.begin(), alphaChunks.end(), [](bitmap_t el){ FreeImage_Unload(el.get()); });
	}

	bool addImgFile(const char* pathName, int flag = 0)  {
		auto img = GenericLoader(pathName, flag);
		if (img == NULL)
			return false;
		chunks.push_back(img);
		return true;
	};

	bool addAlphaFile(const char* pathName, int flag = 0) {
		auto img = GenericLoader(pathName, flag);
		if (img == NULL)
			return false;
		alphaChunks.push_back(img);
		return true;
	};

	virtual bitmap_t finalize(bool showProgress = false) = 0;

	bool finalizeAndSave(const char* outputPath) {
		std::cout << "finalize & safe " << outputPath << std::endl;
		auto img = finalize();
		return 	GenericWriter(img, outputPath, EXR_FLOAT);
	};

};

class AddTaskCollector : public TaskCollector {

public:
	bitmap_t finalize(bool showProgress = false) {
		if (chunks.empty()) {
			return NULL;
		}
		if (showProgress) {
			printf("Adding all accepted chunks to the final image\n");
		}

		auto it = chunks.begin();
		unsigned int width = FreeImage_GetWidth(it->get());
		unsigned int height = FreeImage_GetHeight(it->get());
		FREE_IMAGE_TYPE type = FreeImage_GetImageType(it->get());

		auto finalImage = std::make_shared<FIBITMAP>(*FreeImage_Copy(it->get(), 0, height, width, 0));

		auto chunksWorker = [type, height, width, finalImage](bitmap_t el)
		{
			if (type == FIT_RGBF)
			{
				for (unsigned int y = 0; y < height; ++y) {
					FIRGBF *srcbits = (FIRGBF *)FreeImage_GetScanLine(el.get(), y);
					FIRGBF *dstbits = (FIRGBF *)FreeImage_GetScanLine(finalImage.get(), y);

					for (unsigned int x = 0; x < width; ++x) {
						dstbits[x].red += srcbits[x].red;
						dstbits[x].blue += srcbits[x].blue;
						dstbits[x].green += srcbits[x].green;
					}
				}
			}
			else if (type == FIT_RGBAF)
			{
				for (unsigned int y = 0; y < height; ++y) {
					FIRGBAF *srcbits = (FIRGBAF *)FreeImage_GetScanLine(el.get(), y);
					FIRGBAF *dstbits = (FIRGBAF *)FreeImage_GetScanLine(finalImage.get(), y);

					for (unsigned int x = 0; x < width; ++x) {
						dstbits[x].red += srcbits[x].red;
						dstbits[x].blue += srcbits[x].blue;
						dstbits[x].green += srcbits[x].green;
						dstbits[x].alpha += srcbits[x].alpha;
					}
				}
			}
		};

		auto alphaChunksWorker = [height, width, finalImage](bitmap_t el)
		{
			for (unsigned int y = 0; y < height; ++y) {
				FIRGBAF *srcbits = (FIRGBAF *)FreeImage_GetScanLine(el.get(), y);
				FIRGBAF *dstbits = (FIRGBAF *)FreeImage_GetScanLine(finalImage.get(), y);

				for (unsigned int x = 0; x < width; ++x) {
					dstbits[x].alpha += srcbits[x].red + srcbits[x].blue + srcbits[x].green;
				}
			}
		};

		std::for_each(++chunks.begin(), chunks.end(), chunksWorker);
		std::for_each(alphaChunks.begin(), alphaChunks.end(), alphaChunksWorker);

		return finalImage;
	}
};

class PasteTaskCollector : public TaskCollector {

public:
	bitmap_t finalize(bool showProgress = true) {
		if (chunks.empty()) {
			return NULL;
		}
		if (showProgress) {
			printf("Adding all accepted chunks to the final image\n");
		}
		auto it = chunks.begin();
		unsigned int width = FreeImage_GetWidth(it->get());
		unsigned int chunkHeight = FreeImage_GetHeight(it->get());
		unsigned int height = chunkHeight * chunks.size();
		unsigned int currentHeight = height - chunkHeight;

		FREE_IMAGE_TYPE type = FreeImage_GetImageType(it->get());
		int bpp = FreeImage_GetBPP(it->get());
		auto finalImage = std::make_shared<FIBITMAP>(*FreeImage_AllocateT(type, width, height, bpp));

		auto chunkWorker = [type, chunkHeight, finalImage, &currentHeight, width](bitmap_t el)
		{
			if (type == FIT_RGBF)
			{
				for (unsigned int y = 0; y < chunkHeight; ++y) {
					FIRGBF *srcbits = (FIRGBF *)FreeImage_GetScanLine(el.get(), y);
					FIRGBF *dstbits = (FIRGBF *)FreeImage_GetScanLine(finalImage.get(), y + currentHeight);
					for (unsigned int x = 0; x < width; ++x) {
						dstbits[x].red = srcbits[x].red;
						dstbits[x].blue = srcbits[x].blue;
						dstbits[x].green = srcbits[x].green;
					}
				}
			}
			else if (type == FIT_RGBAF)
			{
				for (unsigned int y = 0; y < chunkHeight; ++y) {
					FIRGBAF *srcbits = (FIRGBAF *)FreeImage_GetScanLine(el.get(), y);
					FIRGBAF *dstbits = (FIRGBAF *)FreeImage_GetScanLine(finalImage.get(), y + currentHeight);
					for (unsigned int x = 0; x < width; ++x) {
						dstbits[x].red = srcbits[x].red;
						dstbits[x].blue = srcbits[x].blue;
						dstbits[x].green = srcbits[x].green;
						dstbits[x].alpha = srcbits[x].alpha;
					}
				}
			}
			currentHeight -= chunkHeight;
		};

		std::for_each(chunks.begin(), chunks.end(), chunkWorker);

		return finalImage;
	}

};

int
main(int argc, char *argv[]) {
	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif // FREEIMAGE_LIB

	// initialize your own FreeImage error handler
	FreeImage_SetOutputMessage(FreeImageErrorHandler);

	// print version & copyright infos
	printf("FreeImage version : %s", FreeImage_GetVersion());
	printf("\n");
	printf(FreeImage_GetCopyrightMessage());
	printf("\n");

	if (argc < 4)  {
		printf("Usage: taskcollector.exe <type> <outputfile> <inputfile1> [<input file2> ...]\n");
		return -1;
	}

	std::unique_ptr<TaskCollector> taskCollector;
	std::unique_ptr<TaskCollector> alphaTaskCollector;

	if (strcmp(argv[1], "add") == 0) {
		taskCollector = std::make_unique<AddTaskCollector>();
		alphaTaskCollector = std::make_unique<AddTaskCollector>();
	}
	else if (strcmp(argv[1], "paste") == 0) {
		taskCollector = std::make_unique<AddTaskCollector>();
		alphaTaskCollector = std::make_unique<AddTaskCollector>();
	}
	else {
		printf("Possible types: 'add', 'paste'\n");
		return -1;
	}

	for (int i = 3; i < argc; ++i) {
		if (std::string(argv[i]).find("Alpha") == std::string::npos) {
			if (!taskCollector->addImgFile(argv[i])) {
				printf("Can't add file: %s\n", argv[i]);
			}
		}
		else {
			if (!taskCollector->addAlphaFile(argv[i])) {
				printf("Can't add file: %s\n", argv[i]);
			}
			if (!alphaTaskCollector->addImgFile(argv[i])) {
				printf("Can't add file: %s\n", argv[i]);
			}
		}
	}

	std::string name(argv[2]);
	auto it = name.find_last_of('.');
	name = it == std::string::npos ? name + ".exr" : name.substr(0, it) + ".Alpha.exr";

	taskCollector->finalizeAndSave(argv[2]);
	alphaTaskCollector->finalizeAndSave(name.c_str());

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_DeInitialise();
#endif // FREEIMAGE_LIB

	return 0;
}