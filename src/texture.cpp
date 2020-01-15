#include "texture.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <SDL2/SDL_image.h>
#include "common.h"
#include "debug.h"
#include "bitmap.h"

void TextureImage::loadFromBMP(const std::string& filename, bool checkSize, bool masked) {
	Bitmap bmp;
	bmp.load(filename);

	if (checkSize && (bmp.w != bmp.h || (1 << int(log2(bmp.w))) != bmp.w)) {
		LogWarning("Failed to load file \"" + filename + "\" as bitmap texture: unsupported image size (must be a square with side length 2 ^ n pixels)");
		return;
	}

	mWidth = bmp.w;
	mHeight = bmp.h;
	mBytesPerPixel = masked ? 4 : 3;
	mPitch = alignedPitch(mWidth * mBytesPerPixel);

	if (masked) {
		mData = new unsigned char[mHeight * mPitch];
		memset(mData, 255, sizeof(unsigned char) * mHeight * mPitch);
		for (int i = 0; i < mHeight; i++) for (int j = 0; j < mWidth; j++) {
			mData[i * mPitch + j * mBytesPerPixel + 3] = reinterpret_cast<unsigned char*>(bmp.data)[i * bmp.pitch + j * 3];
		}
	} else {
		mData = new unsigned char[mHeight * mPitch];
		for (int i = 0; i < mHeight; i++) {
			memcpy(mData + i * mPitch, reinterpret_cast<unsigned char*>(bmp.data) + i * bmp.pitch, mWidth * mBytesPerPixel);
		}
	}
}

void TextureImage::loadFromPNG(const std::string& filename, bool checkSize, bool masked) {
	SDL_Surface* surface = IMG_LoadPNG_RW(SDL_RWFromFile(filename.c_str(), "rb"));
	if (surface == nullptr) {
		LogWarning("Failed to load file \"" + filename + "\" as PNG image: " + IMG_GetError());
		return;
	}

	if (checkSize && surface->w != surface->h || (1 << int(log2(surface->w))) != surface->w) {
		LogWarning("Failed to load file \"" + filename + "\" as PNG texture: unsupported image size (must be a square with side length 2 ^ n pixels)");
		return;
	}

	if (surface->format->BytesPerPixel == 1) {
		// Grayscale / alpha
		mWidth = surface->w;
		mHeight = surface->h;
		mBytesPerPixel = 4;
		mPitch = alignedPitch(mWidth * mBytesPerPixel);
		mData = new unsigned char[mHeight * mPitch];
		memset(mData, 255, sizeof(unsigned char) * mHeight * mPitch);
		for (int i = 0; i < mHeight; i++) for (int j = 0; j < mWidth; j++) {
			unsigned char col = reinterpret_cast<unsigned char*>(surface->pixels)[i * surface->pitch + j];
			if (masked) { // Alpha
				mData[i * mPitch + j * mBytesPerPixel + 3] = col;
			} else { // Grayscale
				mData[i * mPitch + j * mBytesPerPixel + 0] = col;
				mData[i * mPitch + j * mBytesPerPixel + 1] = col;
				mData[i * mPitch + j * mBytesPerPixel + 2] = col;
			}
		}
	} else if (surface->format->BytesPerPixel == 3 || surface->format->BytesPerPixel == 4) {
		// RGB / RGBA
		if (masked) {
			LogWarning("Failed to load file \"" + filename + "\" as mask PNG image: unsupported format (only grayscale PNG-8 is supported)");
			return;
		}
		mWidth = surface->w;
		mHeight = surface->h;
		mBytesPerPixel = surface->format->BytesPerPixel;
		mPitch = alignedPitch(mWidth * mBytesPerPixel);
		mData = new unsigned char[mHeight * mPitch];
		for (int i = 0; i < mHeight; i++) {
			memcpy(mData + i * mPitch, reinterpret_cast<unsigned char*>(surface->pixels) + i * surface->pitch, mWidth * mBytesPerPixel);
		}
	} else {
		LogWarning("Failed to load file \"" + filename + "\" as PNG image: unsupported format (only PNG-8/24/32 is supported)");
		return;
	}

	SDL_FreeSurface(surface);
}

void TextureImage::copyFrom(const TextureImage& src, int x, int y, int srcx, int srcy) {
	if (src.mBytesPerPixel != mBytesPerPixel) {
		std::stringstream ss;
		ss << "Failed to copy image: expected " << mBytesPerPixel << " bytes per pixel, given " << src.mBytesPerPixel;
		LogWarning(ss.str());
		return;
	}

	if (x < 0) srcx -= x, x = 0;
	if (y < 0) srcy -= y, y = 0;
	if (srcx < 0) x -= srcx, srcx = 0;
	if (srcy < 0) y -= srcy, srcy = 0;

	int width = std::min(mWidth - x, src.mWidth - srcx), height = std::min(mHeight - y, src.mHeight - srcy);
	if (width <= 0 || height <= 0) return;

	for (int i = 0; i < height; i++) {
		memcpy(
			mData + (i + y) * mPitch + x * mBytesPerPixel,
			src.mData + (i + srcy) * src.mPitch + srcx * src.mBytesPerPixel,
			width * mBytesPerPixel * sizeof(unsigned char)
		);
	}
}

TextureImage TextureImage::convert(int bytesPerPixel) const {
	TextureImage res(mWidth, mHeight, bytesPerPixel);
	for (int i = 0; i < mHeight; i++)
		for (int j = 0; j < mWidth; j++) {
			unsigned char* p = res.mData + i * res.mPitch + j * res.mBytesPerPixel;
			unsigned char* pSrc = mData + i * mPitch + j * mBytesPerPixel;
			unsigned char r = 0, g = 0, b = 0, a = 255u;
			if (mBytesPerPixel == 4) r = pSrc[0], g = pSrc[1], b = pSrc[2], a = pSrc[3];
			else r = pSrc[0], g = pSrc[1], b = pSrc[2];
			if (res.mBytesPerPixel == 4) p[0] = r, p[1] = g, p[2] = b, p[3] = a;
			else p[0] = r, p[1] = g, p[2] = b;
		}
	return res;
}

TextureImage TextureImage::enlarge(int scale) const {
	TextureImage res(mWidth * scale, mHeight * scale, mBytesPerPixel);
	for (int i = 0; i < mHeight * scale; i++)
		for (int j = 0; j < mWidth * scale; j++)
			for (int k = 0; k < mBytesPerPixel; k++) {
				res.mData[i * res.mPitch + j * res.mBytesPerPixel + k] = mData[i / scale * mPitch + j / scale * mBytesPerPixel + k];
			}
	return res;
}

TextureImage TextureImage::shrink(int scale) const {
	TextureImage res(mWidth / scale, mHeight / scale, mBytesPerPixel);
	for (int i = 0; i < mHeight / scale; i++)
		for (int j = 0; j < mWidth / scale; j++)
			for (int k = 0; k < mBytesPerPixel; k++) {
				int sum = 0;
				for (int i1 = 0; i1 < scale; i1++)
					for (int j1 = 0; j1 < scale; j1++)
						sum += mData[(i * scale + i1) * mPitch + (j * scale + j1) * mBytesPerPixel + k];
				res.mData[i * res.mPitch + j * res.mBytesPerPixel + k] = static_cast<unsigned char>(sum / scale / scale);
			}
	return res;
}

TextureImage TextureImage::resample(int width, int height) const {
	TextureImage res(width, height, mBytesPerPixel);
	for (int i = 0; i < height; i++)
		for (int j = 0; j < width; j++)
			for (int k = 0; k < mBytesPerPixel; k++) {
				// TODO: use AABB to calculate an average color on destination area
				int i1 = int(double(i) / height * mHeight), j1 = int(double(j) / width * mWidth);
				res.mData[i * res.mPitch + j * res.mBytesPerPixel + k] = empty()? 0 : mData[i1 * mPitch + j1 * mBytesPerPixel + k];
			}
	return res;
}

void Build2DMipmaps(const TextureImage& image, TextureFormat format, int level) {
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, level);
	glTexEnvf(GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, 0.0f);
	Assert(image.bytesPerPixel() == 3 || image.bytesPerPixel() == 4);
	TextureFormat srcFormat = image.bytesPerPixel() == 4 ? TextureFormatRGBA : TextureFormatRGB;
	TextureImage curr;
	curr = image;
	for (int i = 0; i <= level; i++) {
		glTexImage2D(GL_TEXTURE_2D, i, format, curr.width(), curr.height(), 0, srcFormat, GL_UNSIGNED_BYTE, curr.data());
		curr = curr.shrink(2);
	}
}

void Texture::load(const TextureImage& image, bool alpha, bool bilinear, int maxLevels) {
	if (image.data() == nullptr) {
		LogWarning("Skipping empty texture image");
		return;
	}
	Assert(image.bytesPerPixel() == 3 || image.bytesPerPixel() == 4);
	if (maxLevels < 0) maxLevels = int(log2(image.width()));
	TextureFormat format = alpha ? TextureFormatRGBA : TextureFormatRGB;
	glGenTextures(1, &mID);
	glBindTexture(GL_TEXTURE_2D, mID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, bilinear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, bilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR);
	Build2DMipmaps(image, format, maxLevels);
}
