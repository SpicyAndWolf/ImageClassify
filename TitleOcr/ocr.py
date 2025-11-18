from paddleocr import PaddleOCR
import cv2
import os
import platform
import shutil
import argparse
import signal
import logging
import sys
from datetime import datetime
import numpy as np


# ===================== 路径相关全局常量 =====================

# 程序所在目录（兼容 PyInstaller 打包后的 _MEIPASS）
BASE_DIR = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))

# 随程序一起打包的模型目录（允许中文路径）
BUNDLED_MODEL_ROOT = os.path.join(BASE_DIR, "models", "ppocrv5_mobile")

# 供 PaddleOCR 使用的“全局模型目录”（尽量保证纯英文路径）
if platform.system() == "Windows":
    PPOCR_LOCAL_ROOT = r"C:/ProgramData/ppocr_models/ppocrv5_mobile"
else:
    PPOCR_LOCAL_ROOT = os.path.join("/usr", "local", "share", "ppocr_models", "ppocrv5_mobile")

# PaddleOCR 期望的 det/rec 模型目录（最终都放到 PPOCR_LOCAL_ROOT 下）
DET_DIR = os.path.join(PPOCR_LOCAL_ROOT, "ch_PP-OCRv5_det")
REC_DIR = os.path.join(PPOCR_LOCAL_ROOT, "ch_PP-OCRv5_rec")


# ===================== 日志 & 全局状态 =====================

def setup_logging(log_level=logging.INFO):
    # 创建logs目录
    log_dir = "logs"
    if not os.path.exists(log_dir):
        os.makedirs(log_dir)

    # 生成日志文件名（包含时间戳）
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_filename = os.path.join(log_dir, f"ocr_process_{timestamp}.log")

    # 日志格式
    log_format = '%(asctime)s - %(levelname)s - %(funcName)s:%(lineno)d - %(message)s'
    date_format = '%Y-%m-%d %H:%M:%S'

    # 拿一个专用 logger（不一定要用 __name__，给个固定名字也可以）
    logger = logging.getLogger("ocr_app")

    # 先清理掉之前可能存在的 handler，避免重复添加
    logger.handlers.clear()
    logger.setLevel(log_level)

    formatter = logging.Formatter(log_format, datefmt=date_format)

    # 文件输出
    file_handler = logging.FileHandler(log_filename, encoding='utf-8')
    file_handler.setLevel(log_level)
    file_handler.setFormatter(formatter)

    # 控制台输出
    stream_handler = logging.StreamHandler(sys.stdout)
    stream_handler.setLevel(log_level)
    stream_handler.setFormatter(formatter)

    logger.addHandler(file_handler)
    logger.addHandler(stream_handler)

    # 不再向上冒泡到 root，避免和其他库的日志重复
    logger.propagate = False

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


# ===================== 模型相关工具函数 =====================

def _assert_model_ok(model_dir: str, name: str) -> bool:
    """
    检查指定目录下是否存在 PaddleOCR 所需的三件套文件。
    存在返回 True，不完整则记录错误并返回 False。
    """
    files = ["inference.pdmodel", "inference.pdiparams", "inference.pdiparams.info"]
    missing = [f for f in files if not os.path.exists(os.path.join(model_dir, f))]
    if missing:
        logger.error(f"{name} 模型缺文件: {missing}，期望在：{model_dir}")
        return False
    return True


def _copy_bundled_models_to_global() -> bool:
    """
    将随程序打包的模型（BUNDLED_MODEL_ROOT）复制到全局英文目录（PPOCR_LOCAL_ROOT）。
    成功返回 True，失败返回 False。
    """
    if not os.path.isdir(BUNDLED_MODEL_ROOT):
        logger.warning(f"未找到内置模型目录: {BUNDLED_MODEL_ROOT}")
        return False

    logger.info(f"准备将内置模型复制到全局目录: {PPOCR_LOCAL_ROOT}")

    try:
        os.makedirs(PPOCR_LOCAL_ROOT, exist_ok=True)

        for sub in ["ch_PP-OCRv5_det", "ch_PP-OCRv5_rec"]:
            src_dir = os.path.join(BUNDLED_MODEL_ROOT, sub)
            dst_dir = os.path.join(PPOCR_LOCAL_ROOT, sub)

            if not os.path.isdir(src_dir):
                logger.error(f"内置模型缺少子目录: {src_dir}")
                return False

            os.makedirs(dst_dir, exist_ok=True)

            for f in ["inference.pdmodel", "inference.pdiparams", "inference.pdiparams.info"]:
                src_file = os.path.join(src_dir, f)
                dst_file = os.path.join(dst_dir, f)
                if not os.path.exists(src_file):
                    logger.error(f"内置模型文件缺失: {src_file}")
                    return False
                shutil.copy2(src_file, dst_file)
                logger.info(f"模型文件复制: {src_file} -> {dst_file}")

        logger.info("内置模型复制到全局目录成功。")
        return True
    except Exception as e:
        logger.error(f"复制内置模型到全局目录失败: {e}")
        return False


def _try_auto_download_models(lang: str = 'ch') -> bool:
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
        try:
            os.makedirs(dst, exist_ok=True)
            for f in ["inference.pdmodel", "inference.pdiparams", "inference.pdiparams.info"]:
                copy2(os.path.join(src, f), os.path.join(dst, f))
            logger.info(f"已复制模型到：{dst}")
        except Exception as e:
            logger.error(f"复制模型到 {dst} 失败: {e}")
            return False

    return True


def _prepare_models(lang: str = 'ch') -> bool:
    """
    模型准备总入口：
    1. 先检查全局英文目录是否已有完整模型
    2. 没有的话，尝试从随程序打包的模型目录复制过去
    3. 如果仍失败，最后尝试在线下载
    """
    # 1) 直接检查全局目录
    if _assert_model_ok(DET_DIR, "检测(det)") and _assert_model_ok(REC_DIR, "识别(rec)"):
        logger.info(f"检测到全局模型目录，直接使用：{PPOCR_LOCAL_ROOT}")
        return True

    logger.warning("全局模型目录不完整，尝试从内置模型复制...")
    if _copy_bundled_models_to_global():
        if _assert_model_ok(DET_DIR, "检测(det)") and _assert_model_ok(REC_DIR, "识别(rec)"):
            logger.info("从内置模型复制成功，将使用全局模型目录。")
            return True
        else:
            logger.error("从内置模型复制后，模型文件仍不完整。")

    logger.warning("尝试在线下载模型（需要网络）...")
    if _try_auto_download_models(lang=lang):
        if _assert_model_ok(DET_DIR, "检测(det)") and _assert_model_ok(REC_DIR, "识别(rec)"):
            logger.info("在线下载并复制模型成功。")
            return True
        else:
            logger.error("在线下载后，模型文件检查仍失败。")
    else:
        logger.error("在线下载模型失败。")

    return False


# ===================== 初始化 PaddleOCR 单例 =====================

if not _prepare_models(lang='ch'):
    logger.error("模型准备失败，无法继续。")
    OCR = None
else:
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


# ===================== OCR 相关功能函数 =====================

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
    x_start = int(width * 0.08)
    y_start = 0
    x_end = int(width)
    y_end = int(height * 0.13)

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
                logger.error("OCR识别失败，将图片复制到错误文件夹")
                error_path = os.path.join(error_folder, image_file)
                failed_images += 1
                try:
                    shutil.copy(image_path, error_path)
                    logger.info(f"图片已复制到错误文件夹: {error_path}")
                except Exception as e:
                    logger.error(f"复制图片到错误文件夹失败: {e}")
        else:
            # 裁剪失败，复制到错误文件夹
            logger.error("图片裁剪失败，将图片复制到错误文件夹")
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


# ===================== 主入口 =====================

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
