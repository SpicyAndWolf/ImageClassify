import easyocr
import cv2
import os
import shutil
from pathlib import Path
import tempfile
import argparse
import signal
import logging
import sys
from datetime import datetime
import ssl
import numpy as np

# 解决在新环境中SSL证书验证失败的问题
# easyocr首次运行时需要下载模型，这可能会因为缺少系统根证书而失败
# 这段代码会尝试禁用SSL证书验证，仅用于下载模型
try:
    _create_unverified_https_context = ssl._create_unverified_context
except AttributeError:
    pass
else:
    ssl._create_default_https_context = _create_unverified_https_context

# 配置日志
def setup_logging(log_level=logging.INFO):
    # 创建logs目录
    log_dir = "logs"
    if not os.path.exists(log_dir):
        os.makedirs(log_dir)
    
    # 生成日志文件名（包含时间戳）
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_filename = os.path.join(log_dir, f"ocr_process_{timestamp}.log")
    
    # 配置日志格式
    log_format = '%(asctime)s - %(levelname)s - %(funcName)s:%(lineno)d - %(message)s'
    date_format = '%Y-%m-%d %H:%M:%S'
    
    # 配置日志记录器
    logging.basicConfig(
        level=log_level,
        format=log_format,
        datefmt=date_format,
        handlers=[
            logging.FileHandler(log_filename, encoding='utf-8'),  # 文件输出
            logging.StreamHandler(sys.stdout)  # 控制台输出
        ]
    )
    
    logger = logging.getLogger(__name__)
    logger.info(f"日志系统初始化完成，日志文件: {log_filename}")
    return logger

# 初始化日志记录器
logger = setup_logging()

# 全局标志用于控制程序终止
should_stop = False

def signal_handler(signum, frame):
    global should_stop
    logger.warning("\n收到终止信号，正在安全退出...")
    should_stop = True

# 注册信号处理器
signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

# 用于把ocr模型移动至目标目录，这样就不用下载了
def copy_file_to_easyocr_model_dir(filename):
    """
    将指定文件从当前目录复制到当前用户的EasyOCR模型目录下。

    Args:
        filename (str): 要复制的文件名。

    Returns:
        bool: 如果复制成功返回 True，否则返回 False。
    """
    try:
        # 获取源文件路径
        source_path = os.path.join(os.getcwd(), "pth", filename)

        # 检查源文件是否存在
        if not os.path.exists(source_path):
            logger.error(f"错误：源文件 '{source_path}' 不存在。")
            return False

        # 获取用户主目录
        user_home_dir = os.path.expanduser('~')
        destination_dir = os.path.join(user_home_dir, '.EasyOCR', 'model')

        # 验证文件是否已存在
        final_path = os.path.join(destination_dir, filename)
        if os.path.exists(final_path):
            logger.info(f"文件已成功位于: '{final_path}'")
            return True

        # 确保目标目录存在
        os.makedirs(destination_dir, exist_ok=True)

        # 执行复制操作
        logger.info(f"正在复制 '{source_path}' 至 '{destination_dir}'...")
        shutil.copy2(source_path, destination_dir)
        
        # 验证文件是否真的复制过去了
        if os.path.exists(final_path):
            logger.info(f"文件已成功位于: '{final_path}'")
            return True
        else:
            logger.error("错误：复制后文件验证失败。")
            return False

    except PermissionError:
        logger.error("错误：权限不足。请尝试使用管理员权限运行此脚本。")
        return False
    except Exception as e:
        logger.error(f"发生未知错误: {e}")
        return False

# 识别图片并返回识别结果
def ocrImg(img_data):
    reader = easyocr.Reader(['en'])

    # 获取图片宽度，用于判断左右侧
    if img_data is None:
        logger.error("图像数据为空")
        return None
    image_height, image_width, _ = img_data.shape

    # 从图片中识别文本，直接传递图像数据
    results = reader.readtext(img_data)

    # 筛选并提取左侧的目标文本
    target_text = ""
    for (bbox, text, prob) in results:
        top_left_x = bbox[0][0]
        if top_left_x < image_width / 10:
            target_text = text
            break # 找到第一个满足条件的就停止

    # 处理结果
    if target_text:
        text = target_text.split(' ')[0]
        return text
    else:
        logger.warning("未识别到目标文字")
        return None

# 裁剪区域
def cropImg(image_path):
    try:
        # 使用可以处理Unicode路径的方式读取文件到内存缓冲区
        with open(image_path, 'rb') as f:
            img_buffer = np.frombuffer(f.read(), dtype=np.uint8)
        
        # 从内存缓冲区解码图像
        img = cv2.imdecode(img_buffer, cv2.IMREAD_COLOR)

        if img is None:
            logger.error(f"无法解码图片: {image_path}")
            return None
    except Exception as e:
        logger.error(f"读取图片时发生错误 {image_path}: {e}")
        return None

    # 获取图片高度和宽度
    height, width, _ = img.shape

    # 定义裁剪区域的坐标
    x_start = int(width*0.08)
    y_start = 0
    x_end = int(width)
    y_end = int(height*0.13)

    # 裁剪图片
    cropped_img = img[y_start:y_end, x_start:x_end]
    logger.info(cropped_img.shape)
    
    return cropped_img


# 处理imgs文件夹下的所有图片
def processAllImages(imgs_folder, res_path="./res"):
    global should_stop

    # # 使用 tempfile 来获取一个可靠的临时文件路径，用于存储裁剪结果，这会自动处理权限和路径编码问题
    # temp_fd, temp_cropped_path = tempfile.mkstemp(suffix=".png")
    # os.close(temp_fd) # 只需要路径，所以关闭文件描述符
    # logger.info(f"使用临时文件: {temp_cropped_path}")
    
    # 设置结果文件夹和错误文件夹
    base_folder = res_path
    error_folder = os.path.join(base_folder, "error")

    # 统计变量
    total_images = 0
    processed_images = 0
    categories = set()  # 使用集合来统计不重复的类别
    
    # 检查imgs文件夹是否存在
    if not os.path.exists(imgs_folder):
        logger.error(f"文件夹 {imgs_folder} 不存在")
        return
    
    # 确保base_folder和error_folder存在
    if not os.path.exists(base_folder):
        os.makedirs(base_folder)
        logger.info(f"创建基础文件夹: {base_folder}")
    
    if not os.path.exists(error_folder):
        os.makedirs(error_folder)
        logger.info(f"创建错误文件夹: {error_folder}")
    
    # 获取所有图片文件
    image_extensions = ['.jpg', '.jpeg', '.png', '.bmp', '.tiff']
    image_files = []
    
    for file in os.listdir(imgs_folder):
        file_path = os.path.join(imgs_folder, file)
        if os.path.isfile(file_path):
            _, ext = os.path.splitext(file)
            if ext.lower() in image_extensions:
                image_files.append(file)
    
    # 统计图片总数
    total_images = len(image_files)
    logger.info(f"找到 {total_images} 个图片文件")
    
    # 处理每个图片文件
    for image_file in image_files:
        # 检查是否需要停止
        if should_stop:
            logger.warning("\n程序被用户终止")
            break

        image_path = os.path.join(imgs_folder, image_file)
        logger.info(f"\n处理图片: {image_file}")
        
        # 裁剪图片
        cropped_img = cropImg(image_path)
        if cropped_img is not None:
            # OCR识别
            ocr_result = ocrImg(cropped_img)
            
            if ocr_result:
                # 尝试创建目标文件夹
                ocr_result = ocr_result.replace("/", "_")
                categories.add(ocr_result) 
                target_folder = os.path.join(base_folder, ocr_result)
                try:
                    if not os.path.exists(target_folder):
                        os.makedirs(target_folder)
                        logger.info(f"创建文件夹: {target_folder}")
                    
                    # 复制图片到目标文件夹
                    target_path = os.path.join(target_folder, image_file)
                    shutil.copy(image_path, target_path)
                    logger.info(f"图片已复制到: {target_path}")
                    processed_images += 1
                    
                except Exception as e:
                    # 创建文件夹失败，复制到错误文件夹
                    logger.error(f"创建文件夹失败: {e}，将图片复制到错误文件夹")
                    error_path = os.path.join(error_folder, image_file)
                    try:
                        shutil.copy(image_path, error_path)
                        logger.info(f"图片已复制到错误文件夹: {error_path}")
                    except Exception as move_error:
                        logger.error(f"复制图片到错误文件夹也失败: {move_error}")
            else:
                # OCR识别失败，复制到错误文件夹
                logger.error(f"OCR识别失败，将图片复制到错误文件夹")
                error_path = os.path.join(error_folder, image_file)
                try:
                    shutil.copy(image_path, error_path)
                    logger.info(f"图片已复制到错误文件夹: {error_path}")
                except Exception as e:
                    logger.error(f"复制图片到错误文件夹失败: {e}")
        else:
            # 裁剪失败，复制到错误文件夹
            logger.error(f"图片裁剪失败，将图片复制到错误文件夹")
            error_path = os.path.join(error_folder, image_file)
            try:
                shutil.copy(image_path, error_path)
                logger.info(f"图片已复制到错误文件夹: {error_path}")
            except Exception as e:
                logger.error(f"复制图片到错误文件夹失败: {e}")
    
    # 清理临时文件
    if os.path.exists(temp_cropped_path):
        os.remove(temp_cropped_path)
    
    if should_stop:
        logger.warning("\n处理被中断！")
    else:
        return {
            'total': total_images,
            'processed': processed_images,
            'categories': categories
        }
    

# 调用函数
if __name__ == "__main__":
    # 初始化模型文件，避免下载时网络问题
    copy_file_to_easyocr_model_dir("craft_mlt_25k.pth")
    copy_file_to_easyocr_model_dir("english_g2.pth")

    # 获取待处理图像所在文件路径
    parser = argparse.ArgumentParser(description="OCR and Image Processing")
    parser.add_argument('--resPath', '-o', default='./res', help='输出文件夹路径')
    parser.add_argument('paths', nargs='+', help='图片文件夹路径列表')
    args = parser.parse_args()

    # 初始化统计变量
    all_total_images = 0
    all_processed_images = 0
    all_categories = set()

    for path in args.paths:
        stats = processAllImages(path, args.resPath)
        if stats:
            all_total_images += stats['total']
            all_processed_images += stats['processed']
            all_categories.update(stats['categories'])

    # 输出总体统计信息
    print(f"FINAL_STATISTICS: TOTAL={all_total_images}, PROCESSED={all_processed_images}, CATEGORIES={len(all_categories)}")