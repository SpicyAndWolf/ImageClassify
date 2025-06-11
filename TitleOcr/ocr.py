import easyocr
import cv2
import os
import shutil
from pathlib import Path
import argparse
import signal
import sys

# 全局标志用于控制程序终止
should_stop = False

def signal_handler(signum, frame):
    global should_stop
    print("\n收到终止信号，正在安全退出...")
    should_stop = True

# 注册信号处理器
signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

# 识别图片并返回识别结果
def ocrImg(image_path):
    # 1. 初始化EasyOCR Reader
    # ['en'] 表示我们希望识别英文
    reader = easyocr.Reader(['en'])

    # 2. 读取图片并获取其宽度，用于判断左右侧
    img = cv2.imread(image_path)
    if img is None:
        print(f"无法读取图片: {image_path}")
        return None
    
    image_height, image_width, _ = img.shape

    # 3. 从图片中识别文本
    # readtext会返回一个列表，每个元素是 (边界框, 文本, 置信度)
    results = reader.readtext(image_path)

    # 4. 筛选并提取左侧的目标文本
    target_text = ""
    for (bbox, text, prob) in results:
        # bbox 是一个包含四个点的列表 [[x1,y1], [x2,y2], [x3,y3], [x4,y4]]
        # 我们取左上角的x坐标来判断位置
        top_left_x = bbox[0][0]
        
        # 判断条件：如果文本的起始位置在图片的左半部分，我们就认为是候选目标
        # 这里的 "/ 10" 是一个经验值，确保只在最左侧
        if top_left_x < image_width / 10:
            target_text = text
            break # 找到第一个满足条件的就停止

    # 5. 处理结果
    if target_text:
        text = target_text.split(' ')[0]
        print(f"识别出的目标文字是: {text}")
        return text
    else:
        print(f"未识别到目标文字: {image_path}")
        return None

# 裁剪区域
def cropImg(image_path, output_path):
    # 1. 读取图片
    img = cv2.imread(image_path)
    if img is None:
        print(f"无法读取图片: {image_path}")
        return False

    # 2. 获取图片高度和宽度
    height, width, _ = img.shape

    # 3. 定义裁剪区域的坐标
    x_start = int(width*0.08)
    y_start = 0
    x_end = int(width)
    y_end = int(height*0.13)

    # 4. 裁剪图片
    cropped_img = img[y_start:y_end, x_start:x_end]

    # 5. 保存裁剪后的图片
    cv2.imwrite(output_path, cropped_img)
    return True

# 处理imgs文件夹下的所有图片
def processAllImages(imgs_folder):
    global should_stop
    temp_cropped = "temp_cropped.png"
    base_folder = "./res"
    error_folder = os.path.join(base_folder, "error")
    
    # 检查imgs文件夹是否存在
    if not os.path.exists(imgs_folder):
        print(f"文件夹 {imgs_folder} 不存在")
        return
    
    # 确保base_folder和error_folder存在
    if not os.path.exists(base_folder):
        os.makedirs(base_folder)
        print(f"创建基础文件夹: {base_folder}")
    
    if not os.path.exists(error_folder):
        os.makedirs(error_folder)
        print(f"创建错误文件夹: {error_folder}")
    
    # 获取所有图片文件
    image_extensions = ['.jpg', '.jpeg', '.png', '.bmp', '.tiff']
    image_files = []
    
    for file in os.listdir(imgs_folder):
        file_path = os.path.join(imgs_folder, file)
        if os.path.isfile(file_path):
            _, ext = os.path.splitext(file)
            if ext.lower() in image_extensions:
                image_files.append(file)
    
    print(f"找到 {len(image_files)} 个图片文件")
    
    # 处理每个图片文件
    for image_file in image_files:
        # 检查是否需要停止
        if should_stop:
            print("\n程序被用户终止")
            break

        image_path = os.path.join(imgs_folder, image_file)
        print(f"\n处理图片: {image_file}")
        
        # 裁剪图片
        if cropImg(image_path, temp_cropped):
            # OCR识别
            ocr_result = ocrImg(temp_cropped)
            
            if ocr_result:
                # 尝试创建目标文件夹
                ocr_result = ocr_result.replace("/", "_")
                target_folder = os.path.join(base_folder, ocr_result)
                try:
                    if not os.path.exists(target_folder):
                        os.makedirs(target_folder)
                        print(f"创建文件夹: {target_folder}")
                    
                    # 复制图片到目标文件夹
                    target_path = os.path.join(target_folder, image_file)
                    shutil.copy(image_path, target_path)
                    print(f"图片已复制到: {target_path}")
                    
                except Exception as e:
                    # 创建文件夹失败，复制到错误文件夹
                    print(f"创建文件夹失败: {e}，将图片复制到错误文件夹")
                    error_path = os.path.join(error_folder, image_file)
                    try:
                        shutil.copy(image_path, error_path)
                        print(f"图片已复制到错误文件夹: {error_path}")
                    except Exception as move_error:
                        print(f"复制图片到错误文件夹也失败: {move_error}")
            else:
                # OCR识别失败，复制到错误文件夹
                print(f"OCR识别失败，将图片复制到错误文件夹")
                error_path = os.path.join(error_folder, image_file)
                try:
                    shutil.copy(image_path, error_path)
                    print(f"图片已复制到错误文件夹: {error_path}")
                except Exception as e:
                    print(f"复制图片到错误文件夹失败: {e}")
        else:
            # 裁剪失败，复制到错误文件夹
            print(f"图片裁剪失败，将图片复制到错误文件夹")
            error_path = os.path.join(error_folder, image_file)
            try:
                shutil.copy(image_path, error_path)
                print(f"图片已复制到错误文件夹: {error_path}")
            except Exception as e:
                print(f"复制图片到错误文件夹失败: {e}")
    
    # 清理临时文件
    if os.path.exists(temp_cropped):
        os.remove(temp_cropped)
    
    if should_stop:
        print("\n处理被中断！")
    else:
        print("\n所有图片处理完成！")

# 调用函数
if __name__ == "__main__":
    # 获取待处理图像所在文件路径
    parser = argparse.ArgumentParser(description="OCR and Image Processing")
    parser.add_argument('paths', nargs='+', help='图片文件夹路径列表')
    args = parser.parse_args()

    for path in args.paths:
        processAllImages(path)