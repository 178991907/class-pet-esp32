# -*- coding: utf-8 -*-
import re
import json

def parse_locals():
    with open("scripts/baidu_file_page.html", "r", encoding="utf-8") as f:
        html = f.read()
        
    pattern = re.search(r"locals\.mset\((\{.*?\})\);", html)
    if not pattern:
        print("未找到 locals.mset")
        return
        
    locals_str = pattern.group(1)
    
    # 因为这个字符串可能不是标准的 JSON (比如包含 undefined, 或未加双引号的 key 等)
    # 我们用 exec 执行它，或者通过简单的正则和 json 库处理。
    # 我们可以通过在一个安全的环境下把这个 JS 对象字面量转换成 Python 字典。
    # 这里我们只用正则提取一些特定的 key
    keys = ["csrf", "uk", "share_uk", "shareid", "sign", "sign1", "sign2", "sign3", "servertime", "bdstoken", "ctime"]
    data = {}
    for key in keys:
        # 寻找 "key":value 或 key:value
        # 处理字符串，注意有些值是数字，有些是字符串
        match = re.search(rf'"{key}"\s*:\s*("(.*?)"|(\d+)|null|false|true)', locals_str)
        if not match:
            # 尝试不带双引号的 key
            match = re.search(rf'\b{key}\s*:\s*("(.*?)"|(\d+)|null|false|true)', locals_str)
        
        if match:
            val = match.group(1)
            if val.startswith('"'):
                val = match.group(2)
            data[key] = val
        else:
            data[key] = "Not Found"
            
    print("提取到的局部变量:")
    print(json.dumps(data, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    parse_locals()
