# ImageClassify——识别特定格式的简单图片分类工具

## 基本描述

- 对于如上图格式的图片，识别其标题栏，并将相同标题的图片划分到相同的文件夹下
- 基于 Qt+Cpp，没什么技术的小玩具

## 界面

## 其他

Qt 版本 5.14.2，使用 MinGW64，Windows 环境。python 要求配置 torch 环境，python3.12.7。使用 python-embed。

- 安装包指令示例：

  ```
  python -m pip install -t \
      E:\Code\QtProjects\build-ImageClassifyNew-Desktop_Qt_5_14_2_MinGW_64_bit-Debug\debug\python-embed\site-packages \
      --only-binary=:all: --no-cache-dir \
      "paddlepaddle==2.6.*" \
      "paddleocr==2.7.*" \
      "numpy==1.26.4" \
      "opencv-python-headless==4.10.*" \
      "pyclipper==1.3.*" \
      "shapely==2.0.*" \
      "scipy==1.11.*" \
      "setuptools>=68.0.0"


  ```

- `python312._pth` 内容（需创建 `site-packages` 目录）

  ```
  python312.zip
  .

  Lib
  site-packages

  # Uncomment to run site.main() automatically
  import site

  ```
