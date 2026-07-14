# -*- coding: utf-8 -*-
import requests
import time
import json

def get_file_detail_and_try_download():
    surl = "G3M-Q1ztXdHwf7xHjxnRsQ"
    pwd = "yvne"
    uk = "351392754"
    shareid = "11484027511"
    
    headers = {
        "User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Referer": f"https://pan.baidu.com/s/1{surl}?pwd={pwd}",
    }
    
    session = requests.Session()
    
    # 1. 访问主页
    session.get(f"https://pan.baidu.com/s/1{surl}?pwd={pwd}", headers=headers)
    
    # 2. 验证提取码
    t = int(time.time() * 1000)
    verify_url = f"https://pan.baidu.com/share/verify?surl={surl}&t={t}&channel=chunlei&web=1&app_id=250528&clienttype=0"
    r_verify = session.post(verify_url, data={"pwd": pwd, "vcode": "", "vcode_str": ""}, headers=headers)
    res_json = r_verify.json()
    randsk = res_json.get("randsk")
    print(f"验证结果 randsk: {randsk}")
    
    if not randsk:
        print("验证失败，未获取到 randsk")
        return
        
    # 3. 获取子目录列表以提取文件的 fs_id
    dir_path = "/sharelink351392754-130938832356199/2.8inch_ESP32-S3"
    list_url = f"https://pan.baidu.com/share/list?uk={uk}&shareid={shareid}&order=other&desc=1&showempty=0&web=1&page=1&num=100&dir={dir_path}&sekey={randsk}"
    
    r_list = session.get(list_url, headers=headers)
    list_res = r_list.json()
    print("列表返回数据:")
    print(json.dumps(list_res, indent=2, ensure_ascii=False))
    
    file_list = list_res.get("list", [])
    if not file_list:
        print("未在子目录中找到文件")
        return
        
    # 找到那个 7z 文件
    target_file = None
    for f in file_list:
        if f.get("server_filename").endswith(".7z"):
            target_file = f
            break
            
    if not target_file:
        print("未找到 .7z 压缩包")
        return
        
    fs_id = target_file.get("fs_id")
    print(f"\n找到目标文件: {target_file.get('server_filename')}")
    print(f"fs_id: {fs_id}")
    
    # 4. 尝试请求 sharedownload 接口
    # 百度分享下载接口参数
    download_api = "https://pan.baidu.com/api/sharedownload?channel=chunlei&clienttype=0&web=1&app_id=250528&bdstoken="
    
    # post data
    # 常见的 post data 包含：
    # encrypt_type: 0
    # extra: {"sekey": "..."}
    # fid_list: [fs_id]
    # primaryid: shareid
    # uk: share_uk
    post_data = {
        "encrypt_type": "0",
        "extra": json.dumps({"sekey": randsk}),
        "fid_list": json.dumps([fs_id]),
        "primaryid": shareid,
        "uk": uk
    }
    
    # 有些解析器说：免登录下载还需要 bdstoken, sign, timestamp. 如果没有，可能会报错。
    # 让我们尝试一下，看看报错是什么，以便知道接下来的方向。
    print("\n--- 尝试请求 sharedownload 接口 ---")
    headers_dl = headers.copy()
    headers_dl["X-Requested-With"] = "XMLHttpRequest"
    
    r_dl = session.post(download_api, data=post_data, headers=headers_dl)
    print("下载接口状态码:", r_dl.status_code)
    try:
        dl_res = r_dl.json()
        print("下载接口返回:")
        print(json.dumps(dl_res, indent=2, ensure_ascii=False))
    except Exception as e:
        print("解析下载接口 JSON 失败")
        print(r_dl.text[:500])

if __name__ == "__main__":
    get_file_detail_and_try_download()
