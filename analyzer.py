#!/usr/bin/env python3
import os
import sys
import subprocess
import argparse
import tempfile
import shutil
import platform
import time
import signal
import psutil

def check_dependencies_linux():
    """检查Linux下的必要依赖"""
    required = ['perf', 'git']
    missing = []
    for cmd in required:
        if not shutil.which(cmd):
            missing.append(cmd)
    if missing:
        print(f"错误: 缺少必要工具: {', '.join(missing)}")
        print("请安装以下工具:")
        print("  perf: sudo apt-get install linux-tools-common linux-tools-generic")
        print("  git: sudo apt-get install git")
        sys.exit(1)

def check_dependencies_windows():
    """检查Windows下的必要依赖"""
    required = ['git']
    missing = []
    for cmd in required:
        if not shutil.which(cmd):
            missing.append(cmd)
    if missing:
        print(f"错误: 缺少必要工具: {', '.join(missing)}")
        print("请安装Git: https://git-scm.com/download/win")
        sys.exit(1)
    
    # 检查MSVC调试器
    try:
        msvc_path = find_msvc_debugger()
        if not msvc_path:
            print("错误: 找不到MSVC调试器 (cdb.exe)")
            print("请安装Visual Studio或Windows SDK")
            print("下载链接: https://visualstudio.microsoft.com/downloads/")
            sys.exit(1)
    except Exception as e:
        print(f"查找MSVC调试器时出错: {e}")
        sys.exit(1)

def find_msvc_debugger():
    """查找Windows下的MSVC调试器路径"""
    # 常见安装路径
    possible_paths = [
        os.path.join(os.environ.get('ProgramFiles(x86)', '')), 
        os.path.join(os.environ.get('ProgramFiles', '')), 
        os.path.join(os.environ.get('SystemDrive', 'C:'), 'Program Files (x86)'),
        os.path.join(os.environ.get('SystemDrive', 'C:'), 'Program Files')
    ]
    
    # 可能的版本路径
    versions = ['2022', '2019', '2017', '10']
    
    for base_path in possible_paths:
        for version in versions:
            debugger_path = os.path.join(
                base_path, 
                'Windows Kits', 
                '10', 
                'Debuggers', 
                'x64', 
                'cdb.exe'
            )
            if os.path.exists(debugger_path):
                return debugger_path
            
            vs_path = os.path.join(
                base_path, 
                'Microsoft Visual Studio', 
                version, 
                'Community', 
                'VC', 
                'Tools', 
                'MSVC'
            )
            if os.path.exists(vs_path):
                # 查找最新的MSVC版本
                versions = sorted(os.listdir(vs_path), reverse=True)
                for ver in versions:
                    debugger_path = os.path.join(
                        vs_path, 
                        ver, 
                        'bin', 
                        'Hostx64', 
                        'x64', 
                        'cdb.exe'
                    )
                    if os.path.exists(debugger_path):
                        return debugger_path
    return None

def setup_flamegraph():
    """设置FlameGraph工具"""
    flamegraph_dir = os.path.join(tempfile.gettempdir(), "FlameGraph")
    if not os.path.exists(flamegraph_dir):
        print("下载FlameGraph工具...")
        subprocess.check_call([
            'git', 'clone', 'https://github.com/brendangregg/FlameGraph.git',
            flamegraph_dir
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return flamegraph_dir

def run_profiling_linux(program, args, perf_data):
    """在Linux下运行程序并使用perf记录性能数据"""
    cmd = ['perf', 'record', '-F', '99', '-g', '-o', perf_data, '--'] + [program] + args
    try:
        print(f"运行程序: {' '.join(cmd)}")
        subprocess.check_call(cmd)
    except subprocess.CalledProcessError as e:
        print(f"程序执行失败: {e}")
        sys.exit(1)

def run_profiling_windows(program, args, flamegraph_dir, etl_file):
    """在Windows下运行程序并收集性能数据"""
    # 获取cdb路径
    cdb_path = find_msvc_debugger()
    if not cdb_path:
        print("错误: 找不到MSVC调试器 (cdb.exe)")
        sys.exit(1)
    
    # 创建XPerf命令
    script_path = os.path.join(flamegraph_dir, 'wincollecttrace.cmd')
    
    # 构造完整命令
    full_cmd = [program] + args
    cmd_str = ' '.join(f'"{arg}"' if ' ' in arg else arg for arg in full_cmd)
    
    # 运行收集脚本
    cmd = [
        'cmd.exe', '/c',
        script_path,
        '-start', 'base',
        '-profileSource', 'Microsoft-Windows-Kernel-Process',
        '-start', 'utc',
        '-cdbexe', f'"{cdb_path}"',
        '-runexe', f'"{cmd_str}"',
        '-stop', 'utc',
        '-stop', 'base',
        '-out', etl_file
    ]
    
    try:
        print(f"开始性能数据收集...")
        print(f"命令: {' '.join(cmd)}")
        subprocess.check_call(cmd)
    except subprocess.CalledProcessError as e:
        print(f"性能数据收集失败: {e}")
        sys.exit(1)

def generate_flamegraph_linux(perf_data, output_svg, flamegraph_dir):
    """在Linux下生成火焰图"""
    # 生成perf脚本输出
    perf_script = os.path.join(tempfile.gettempdir(), "perf.out")
    with open(perf_script, 'w') as f:
        subprocess.check_call(['perf', 'script', '-i', perf_data], stdout=f)
    
    # 折叠堆栈
    folded_data = os.path.join(tempfile.gettempdir(), "folded.out")
    collapse_cmd = os.path.join(flamegraph_dir, 'stackcollapse-perf.pl')
    with open(folded_data, 'w') as f:
        with open(perf_script, 'r') as infile:
            subprocess.check_call(['perl', collapse_cmd], stdin=infile, stdout=f)
    
    # 生成火焰图
    flamegraph_cmd = os.path.join(flamegraph_dir, 'flamegraph.pl')
    with open(output_svg, 'w') as f:
        with open(folded_data, 'r') as infile:
            subprocess.check_call(['perl', flamegraph_cmd], stdin=infile, stdout=f)
    
    print(f"火焰图已生成: {output_svg}")

def generate_flamegraph_windows(etl_file, output_svg, flamegraph_dir):
    """在Windows下生成火焰图"""
    # 首先将ETL文件转换为CSV
    csv_file = etl_file.replace('.etl', '.csv')
    wpa_script = os.path.join(flamegraph_dir, 'wpaexporter.ps1')
    
    # 运行PowerShell脚本转换数据
    try:
        print("转换ETL数据为CSV...")
        subprocess.check_call([
            'powershell.exe', 
            '-ExecutionPolicy', 'Bypass', 
            '-File', wpa_script, 
            etl_file, 
            csv_file
        ])
    except subprocess.CalledProcessError as e:
        print(f"ETL转换失败: {e}")
        sys.exit(1)
    
    # 折叠堆栈
    folded_data = csv_file.replace('.csv', '.folded')
    collapse_cmd = os.path.join(flamegraph_dir, 'stackcollapse-wpa.pl')
    with open(folded_data, 'w') as f:
        with open(csv_file, 'r') as infile:
            subprocess.check_call(['perl', collapse_cmd], stdin=infile, stdout=f)
    
    # 生成火焰图
    flamegraph_cmd = os.path.join(flamegraph_dir, 'flamegraph.pl')
    with open(output_svg, 'w') as f:
        with open(folded_data, 'r') as infile:
            subprocess.check_call(['perl', flamegraph_cmd], stdin=infile, stdout=f)
    
    print(f"火焰图已生成: {output_svg}")

def main():
    parser = argparse.ArgumentParser(description='跨平台C++程序性能分析脚本')
    parser.add_argument('program', help='要分析的C++程序路径')
    parser.add_argument('args', nargs=argparse.REMAINDER, help='程序参数')
    parser.add_argument('-o', '--output', default='flamegraph.svg', help='火焰图输出文件')
    args = parser.parse_args()

    current_os = platform.system()
    
    if current_os == 'Linux':
        # Linux平台
        check_dependencies_linux()
        flamegraph_dir = setup_flamegraph()
        
        with tempfile.NamedTemporaryFile(delete=False, suffix='.perf') as tmpfile:
            perf_data = tmpfile.name
        
        try:
            run_profiling_linux(args.program, args.args, perf_data)
            generate_flamegraph_linux(perf_data, args.output, flamegraph_dir)
        finally:
            if os.path.exists(perf_data):
                os.unlink(perf_data)
    
    elif current_os == 'Windows':
        # Windows平台
        check_dependencies_windows()
        flamegraph_dir = setup_flamegraph()
        
        # 创建临时ETL文件
        with tempfile.NamedTemporaryFile(delete=False, suffix='.etl') as tmpfile:
            etl_file = tmpfile.name
        
        try:
            run_profiling_windows(args.program, args.args, flamegraph_dir, etl_file)
            generate_flamegraph_windows(etl_file, args.output, flamegraph_dir)
        finally:
            if os.path.exists(etl_file):
                os.unlink(etl_file)
    
    else:
        print(f"错误: 不支持的操作系统: {current_os}")
        sys.exit(1)

if __name__ == "__main__":
    main()
