from paddleocr import PaddleOCR
import cv2
import os
import shutil
import argparse
import signal
import logging
import sys
from datetime import datetime
import numpy as np


# 模型不在本地时下载
def _try_auto_download_models(lang: str = 'ch'):
    """
    尝试用 PaddleOCR 的内置下载器拉取模型到 ~/.paddleocr，
    下载完成后，把 det/rec 模型目录复制到我们期望的 DET_DIR/REC_DIR。
    """
    from shutil import copy2
    from glob import glob

    logger.warning("未找到本地模型，尝试使用 PaddleOCR 内置下载器在线获取...")

    # 1) 触发下载（不传 det/rec 路径）
    try:
        _tmp_ocr = PaddleOCR(lang=lang, use_angle_cls=False, use_gpu=False, show_log=False)
        logger.info("内置下载完成（或缓存可用），开始定位模型路径...")
    except Exception as e:
        logger.error(f"内置下载失败：{e}")
        return False

    # 2) 在 ~/.paddleocr 下寻找 ch 的 det/rec infer 目录（按 v5/未来版本命名变化做模糊匹配）
    home = os.path.expanduser("~")
    cache_root = os.path.join(home, ".paddleocr")
    # 可能的子目录（不同版本/whl会有层级差异，做几种候选）
    candidates = [
        cache_root,
        os.path.join(cache_root, "whl"),
        os.path.join(cache_root, "models"),
    ]

    def _find_infer_dir(patterns):
        for base in candidates:
            for pat in patterns:
                matches = glob(os.path.join(base, pat), recursive=True)
                for p in matches:
                    # 需要包含三件套文件
                    ok = all(os.path.exists(os.path.join(p, f)) for f in
                             ["inference.pdmodel", "inference.pdiparams", "inference.pdiparams.info"])
                    if ok:
                        return p
        return None

    # 尽量匹配 PP-OCRv5，其次泛匹配任意 ch_*_det/rec_infer
    det_src = _find_infer_dir([
        "**/ch_PP-OCRv5_*det*infer*",
        "**/ch_*det*infer*",
    ])
    rec_src = _find_infer_dir([
        "**/ch_PP-OCRv5_*rec*infer*",
        "**/ch_*rec*infer*",
    ])

    if not det_src or not rec_src:
        logger.error(f"未能在缓存中找到下载后的 det 或 rec 目录：det={det_src}, rec={rec_src}")
        return False

    # 3) 复制到你的目标目录（DET_DIR/REC_DIR）
    for src, dst in [(det_src, DET_DIR), (rec_src, REC_DIR)]:
        os.makedirs(dst, exist_ok=True)
        for f in ["inference.pdmodel", "inference.pdiparams", "inference.pdiparams.info"]:
            copy2(os.path.join(src, f), os.path.join(dst, f))
        logger.info(f"已复制模型到：{dst}")

    return True

# 检验模型是否在本地
def _assert_model_ok(model_dir: str, name: str):
    files = ["inference.pdmodel", "inference.pdiparams", "inference.pdiparams.info"]
    missing = [f for f in files if not os.path.exists(os.path.join(model_dir, f))]
    if missing:
        logger.error(f"{name} 模型缺文件: {missing}，期望在：{model_dir}")
        sys.exit(2)

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
            logging.FileHandler(log_filename, encoding='utf-8'),
            logging.StreamHandler(sys.stdout)
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

# ==== PaddleOCR 全局单例与本地模型路径 ====
BASE_DIR = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
PPOCR_LOCAL_ROOT = os.path.join(BASE_DIR, "models", "ppocrv5_mobile")

# 你随包分发的本地模型目录（确保三文件齐全）
DET_DIR = os.path.join(PPOCR_LOCAL_ROOT, "ch_PP-OCRv5_det")
REC_DIR = os.path.join(PPOCR_LOCAL_ROOT, "ch_PP-OCRv5_rec")

# 校验模型是否存在
need_try_download = False
try:
    _assert_model_ok(DET_DIR, "检测(det)")
    _assert_model_ok(REC_DIR, "识别(rec)")
except SystemExit:
    need_try_download = True

if need_try_download:
    if not _try_auto_download_models(lang='ch'):
        logger.error("模型下载/复制失败，无法继续。")
        sys.exit(2)

# 初始化 PaddleOCR（模块级单例，避免重复创建）
try:
    OCR = PaddleOCR(
        use_angle_cls=False, 
        lang='ch',
        det_model_dir=DET_DIR,
        rec_model_dir=REC_DIR,
        show_log=False,
        use_gpu=False
    )
    logger.info(f"PaddleOCR 初始化成功，det={DET_DIR}, rec={REC_DIR}")
except Exception as e:
    logger.error(f"PaddleOCR 初始化失败：{e}")
    OCR = None

# 识别图片并返回识别结果
def ocrImg(img_data):
    if img_data is None:
        logger.error("图像数据为空")
        return None
    if OCR is None:
        logger.error("PaddleOCR 未初始化")
        return None

    # OpenCV BGR -> 直接传 numpy 数组即可
    # 返回结构: [ [ [ [x1,y1],...,[x4,y4] ], (text, score) ], ... ]
    try:
        result = OCR.ocr(img_data, cls=False)
    except Exception as e:
        logger.error(f"OCR 识别异常：{e}")
        return None

    if not result or not result[0]:
        logger.warning("未识别到文本")
        return None

    lines = result[0]
    image_height, image_width, _ = img_data.shape

    # 取最靠左且在上1/3区域的第一个文本
    target_text = ""
    for line in lines:
        box = line[0]              # 4 点坐标
        text, prob = line[1]       # 文本与分数
        top_left_x = box[0][0]
        if top_left_x < image_width / 10:
            target_text = text
            break

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

# 检查图片是否已经被分类
def is_image_already_classified(image_file, base_folder):
    """检查图片是否已经在某个分类文件夹中"""
    if not os.path.exists(base_folder):
        return False
        
    # 遍历所有子文件夹
    for item in os.listdir(base_folder):
        item_path = os.path.join(base_folder, item)
        if os.path.isdir(item_path):
            # 检查该文件夹中是否存在同名图片
            target_file = os.path.join(item_path, image_file)
            if os.path.exists(target_file):
                logger.info(f"图片 {image_file} 已存在于分类文件夹 {item} 中，跳过处理")
                return True
    return False

# 处理imgs文件夹下的所有图片
def processAllImages(imgs_folder, res_path="./res"):
    global should_stop
    
    # 设置结果文件夹和错误文件夹
    base_folder = res_path
    error_folder = os.path.join(base_folder, "error")

    # 统计变量
    total_images = 0
    processed_images = 0
    skipped_images = 0 
    failed_images = 0
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

    # 计算完 total_images 之后，先把初始 STAT 发给前端
    print(f"STAT total={total_images} ok=0 fail=0", flush=True)
    print("PROGRESS 0%", flush=True)
    
    # 处理每个图片文件
    for image_file in image_files:
        # 检查是否需要停止
        if should_stop:
            logger.warning("\n程序被用户终止")
            break

        # 检查图片是否已经被分类
        if is_image_already_classified(image_file, base_folder):
            skipped_images += 1
            continue

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
                    failed_images += 1
                    try:
                        shutil.copy(image_path, error_path)
                        logger.info(f"图片已复制到错误文件夹: {error_path}")
                    except Exception as move_error:
                        logger.error(f"复制图片到错误文件夹也失败: {move_error}")
            else:
                # OCR识别失败，复制到错误文件夹
                logger.error(f"OCR识别失败，将图片复制到错误文件夹")
                error_path = os.path.join(error_folder, image_file)
                failed_images += 1
                try:
                    shutil.copy(image_path, error_path)
                    logger.info(f"图片已复制到错误文件夹: {error_path}")
                except Exception as e:
                    logger.error(f"复制图片到错误文件夹失败: {e}")
        else:
            # 裁剪失败，复制到错误文件夹
            logger.error(f"图片裁剪失败，将图片复制到错误文件夹")
            error_path = os.path.join(error_folder, image_file)
            failed_images += 1
            try:
                shutil.copy(image_path, error_path)
                logger.info(f"图片已复制到错误文件夹: {error_path}")
            except Exception as e:
                logger.error(f"复制图片到错误文件夹失败: {e}")

        # 每处理完一张（无论成功/失败/跳过），都更新进度与 STAT：
        done = processed_images + failed_images + skipped_images
        if total_images > 0:
            percent = int(done * 100 / total_images)
            print(f"PROGRESS {percent}%", flush=True)
        print(f"STAT total={total_images} ok={processed_images} fail={failed_images}", flush=True)
    
    if should_stop:
        logger.warning("\n处理被中断！")
    else:
        return {
            'total': total_images,
            'processed': processed_images,
            'skipped': skipped_images,
            'categories': categories
        }
    

# 调用函数
if __name__ == "__main__":
    # 获取待处理图像所在文件路径
    parser = argparse.ArgumentParser(description="OCR and Image Processing")
    parser.add_argument('--resPath', '-o', default='./res', help='输出文件夹路径')
    parser.add_argument('paths', nargs='+', help='图片文件夹路径列表')
    args = parser.parse_args()

    # 初始化统计变量
    all_total_images = 0
    all_processed_images = 0
    all_skipped_images = 0
    all_categories = set()

    for path in args.paths:
        stats = processAllImages(path, args.resPath)
        if stats:
            all_total_images += stats['total']
            all_processed_images += stats['processed']
            all_skipped_images += stats.get('skipped') 
            all_categories.update(stats['categories'])

    # 输出总体统计信息
    logger.info(f"skipped images num:{all_skipped_images}")
    print(f"FINAL_STATISTICS: TOTAL={all_total_images}, PROCESSED={all_processed_images}, CATEGORIES={len(all_categories)}")