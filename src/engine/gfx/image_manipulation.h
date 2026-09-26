#ifndef ENGINE_GFX_IMAGE_MANIPULATION_H
#define ENGINE_GFX_IMAGE_MANIPULATION_H

#include <engine/image.h>

#include <generated/data_types.h>

#include <cstdint>

// Destination must have appropriate size for RGBA data
bool ConvertToRgba(uint8_t *pDest, const CImageInfo &SourceImage);
// Allocates appropriate buffer with malloc, must be freed by caller
bool ConvertToRgbaAlloc(uint8_t *&pDest, const CImageInfo &SourceImage);
// Replaces existing image data with RGBA data (unless already RGBA)
bool ConvertToRgba(CImageInfo &Image);

// Changes the image data (not the format)
void ConvertToGrayscale(const CImageInfo &Image);
void ConvertToGrayscaleRect(const CImageInfo &Image, size_t StartX, size_t StartY, size_t Width, size_t Height);

// Color a rectangle inside an image with hue and saturation
void ColorizeWithHueRect(CImageInfo &Image, float Hue, float Sat, size_t StartX, size_t StartY, size_t Width, size_t Height);

// These functions assume that the image data is 4 bytes per pixel RGBA
void DilateImage(uint8_t *pImageBuff, int w, int h);
void DilateImage(const CImageInfo &Image);
void DilateImageSub(uint8_t *pImageBuff, int w, int h, int x, int y, int SubWidth, int SubHeight);

// Returned buffer is allocated with malloc, must be freed by caller
uint8_t *ResizeImage(const uint8_t *pImageData, int Width, int Height, int NewWidth, int NewHeight, int BPP);
// Replaces existing image data with resized buffer
void ResizeImage(CImageInfo &Image, int NewWidth, int NewHeight);

int HighestBit(int OfVar);

// 图集网格换算：把 sprite 的格坐标解析成图集内的像素矩形（OutX/OutY 为左上角）。
// 网格非法、图集尺寸不能被网格整除或 sprite 越界时返回 false。
// pOutOfBounds 非空时额外区分「sprite 越界」与「参数非法」两类失败。
bool ResolveSpritePixelRect(size_t ImageWidth, size_t ImageHeight, int GridX, int GridY,
	int SpriteX, int SpriteY, int SpriteW, int SpriteH,
	size_t &OutX, size_t &OutY, size_t &OutW, size_t &OutH, bool *pOutOfBounds = nullptr);

// 从图集里提取单个 sprite 的像素数据（分配缓冲并复制），供资源解码任务在 CPU 侧调用，
// 与 ResolveSpritePixelRect 共用同一套网格换算，不另起一份实现。
// 图集不可整除、sprite 越界、无像素数据、尺寸非法或分配失败都返回 false，Result 不被写入。
bool ExtractSpriteImage(const CImageInfo &FromImageInfo, const CDataSprite *pSprite, CImageInfo &Result);

// 判断图像中指定矩形是否完全透明。仅对带 alpha 通道的格式（FORMAT_R / FORMAT_RA / FORMAT_RGBA）
// 有效，其它格式、空数据或越界矩形都返回 false。
bool IsImageRectFullyTransparent(const CImageInfo &Image, size_t X, size_t Y, size_t Width, size_t Height);

// 把整张图清成完全透明（所有通道归零）。用于按内置图的尺寸与格式造一张「空白材质」。
void ClearImageToTransparent(CImageInfo &Image);

// 空白 sprite 回退：当 Image 的 (X, Y, Width, Height) 完全透明时，用 FallbackImage 的
// (FallbackX, FallbackY, FallbackWidth, FallbackHeight) 覆盖它。
// 两个区域的像素尺寸一致时按行拷贝，不一致时按最近邻缩放——同一 sprite 在不同分辨率的画布上
// 占同一个格位，所以映射依据是「格比例」而不是绝对像素尺寸。
// 两张图必须同格式（含 alpha）且数据有效，否则不做任何修改。返回是否发生了覆盖。
bool CopyFallbackOverBlankRect(CImageInfo &Image, const CImageInfo &FallbackImage,
	size_t X, size_t Y, size_t Width, size_t Height,
	size_t FallbackX, size_t FallbackY, size_t FallbackWidth, size_t FallbackHeight);

#endif // ENGINE_GFX_IMAGE_MANIPULATION_H
