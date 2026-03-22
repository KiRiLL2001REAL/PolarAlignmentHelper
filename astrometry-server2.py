import socket
import threading
import subprocess
import re
import os
import signal
import time

HOST = '0.0.0.0'  # Слушать все доступные интерфейсы
PORT = 65432      # Произвольный порт выше 1024

# Глобальное состояние для plate-solving

IDLE = 'idle'
INITIALIZING = 'initializing'
RUNNING = 'running'
COMPLETED = 'completed'
FAILED = 'failed'
CANCELLED = 'cancelled'
TIMEOUT = 'timeout'

solve_state = {
    'process': None,
    'thread': None,
    'status': IDLE,  # idle, initializing, running, completed, failed, cancelled, timeout
    'output': None,
    'result': None,
    'error': None,
    'start_time': None
}
state_lock = threading.Lock()


def parse_solve_output(output_text):
    """
    Парсит вывод solve-field и извлекает требуемые значения.
    Возвращает dict с данными или None при ошибке.
    """
    result = {}
    
    # Field center (RA, Dec) в градусах
    ra_dec_match = re.search(r'Field center:\s*\(RA,Dec\)\s*=\s*\(([^,]+),\s*([^)]+)\)\s*deg', output_text)
    if ra_dec_match:
        result['ra'] = float(ra_dec_match.group(1).strip())
        result['dec'] = float(ra_dec_match.group(2).strip())
    
    # Field center в формате H:M:S, D:M:S
    hms_match = re.search(r'Field center:\s*\(RA H:M:S,\s*Dec D:M:S\)\s*=\s*\(([^,]+),\s*([^)]+)\)', output_text)
    if hms_match:
        result['ra_hms'] = hms_match.group(1).strip()
        result['dec_dms'] = hms_match.group(2).strip()
    
    # Pixel scale (в строке с RA,Dec)
    scale_match = re.search(r'pixel scale\s+([0-9.]+)\s*arcsec/pix', output_text)
    if scale_match:
        result['pixel_scale'] = float(scale_match.group(1).strip())
    
    # Field size
    size_match = re.search(r'Field size:\s*([^\n]+?)(?:\n|$)', output_text)
    if size_match:
        result['field_size'] = size_match.group(1).strip()
    
    # Field rotation angle
    rotation_match = re.search(r'Field rotation angle:\s*up is\s*([^\n]+?)(?:\n|$)', output_text)
    if rotation_match:
        result['rotation_angle'] = rotation_match.group(1).strip()
    
    # Field parity
    parity_match = re.search(r'Field parity:\s*(\w+)', output_text)
    if parity_match:
        result['parity'] = parity_match.group(1).strip()
    
    return result if result else None


def run_solve_field(image_path, timeout_sec=None):
    """
    Запускает solve-field в отдельном процессе.
    Выполняется в фоне, обновляет глобальное состояние.
    """
    global solve_state
    
    print(f"[INFO] Plate-Solving START")
    
    cmd = [
        'solve-field',
        image_path,
        '--ra', '0',
        '--dec', '90', 
        '--radius', '15',
        '--no-plots',
        '--downsample', '2',
        '--overwrite'
    ]
    
    try:
        with state_lock:
            solve_state['status'] = RUNNING
            solve_state['output'] = ''
            solve_state['result'] = None
            solve_state['error'] = None
            solve_state['start_time'] = time.time()
        
        # Запуск процесса с группой процессов для корректной остановки
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            preexec_fn=os.setsid if os.name != 'nt' else None
        )
        
        with state_lock:
            solve_state['process'] = process
            
        timeout_reached = False
        
        # Чтение вывода построчно c проверкой тайм-аута
        output_lines = []
        for line in iter(process.stdout.readline, ''):
            if line:
                output_lines.append(line)
                if timeout_sec:
                    with state_lock:
                        start = solve_state['start_time']
                    if (time.time() - start) > timeout_sec:
                        cancel_solve(isTimeout=True)
                        timeout_reached = True
        
        process.wait()
        
        full_output = ''.join(output_lines)
        
        with state_lock:
            solve_state['output'] = full_output
        
        # Анализ результата
        if process.returncode != 0 and timeout_reached:
            with state_lock:
                solve_state['status'] = FAILED
                solve_state['error'] = 'CPU time limit reached'
            print(f"[INFO] Plate-Solving CPU LIMIT")
        elif process.returncode == 0 and 'solved with index' in full_output:
            parsed = parse_solve_output(full_output)
            if parsed and parsed.get('ra') is not None and parsed.get('dec') is not None:
                with state_lock:
                    solve_state['status'] = COMPLETED
                    solve_state['result'] = parsed
                print(f"[INFO] Plate-Solving END. RA={parsed.get('ra')}, Dec={parsed.get('dec')}")
            else:
                with state_lock:
                    solve_state['status'] = FAILED
                    solve_state['error'] = 'Failed to parse solution from output'
                print(f"[INFO] Plate-Solving FAILED TO PARSE OUTPUT")
        elif 'Did not solve' in full_output or 'no WCS file was written' in full_output:
            with state_lock:
                solve_state['status'] = FAILED
                solve_state['error'] = 'No solution found'
            print(f"[INFO] Plate-Solving NO SOLUTION")
        else:
            with state_lock:
                reason = solve_state.get('status')
            if reason != CANCELLED:
                with state_lock:
                    solve_state['status'] = FAILED
                    solve_state['error'] = f'Process failed (code: {process.returncode})'
                print(f"[INFO] Plate-Solving PROCESS FAILED (code: {process.returncode})")
                # === ОТЛАДКА: Проверка окружения ===
                import shutil
                solve_path = shutil.which('solve-field')
                print(f"[DEBUG] solve-field найден в: {solve_path}")
                print(f"[DEBUG] image_path: {image_path}")
                print(f"[DEBUG] файл существует: {os.path.exists(image_path)}")
                print(f"[DEBUG] текущая директория: {os.getcwd()}")
                # === КОНЕЦ ОТЛАДКИ ===
                
    except FileNotFoundError:
        with state_lock:
            solve_state['status'] = FAILED
            solve_state['error'] = 'solve-field command not found'
    except Exception as e:
        with state_lock:
            solve_state['status'] = FAILED
            solve_state['error'] = str(e)
    finally:
        with state_lock:
            solve_state['process'] = None


def cancel_solve(isTimeout = None):
    """
    Принудительно останавливает текущий процесс solve-field.
    Возвращает True если остановка успешна.
    """
    global solve_state
    
    with state_lock:
        process = solve_state.get('process')
        
        if isTimeout:
            solve_state['status'] = TIMEOUT
        else:
            solve_state['status'] = CANCELLED
        solve_state['process'] = None
        
        if process and process.poll() is None:
            try:
                # Отправка SIGINT всей группе процессов (для WSL/Linux)
                if os.name != 'nt':
                    os.killpg(os.getpgid(process.pid), signal.SIGINT)
                else:
                    # Для Windows (если понадобится)
                    process.send_signal(signal.CTRL_BREAK_EVENT)
                # Ждём завершения до 5 секунд
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
            except Exception:
                process.kill()
            print(f"[INFO] Plate-Solving CANCELLED")
            return True
    return False


def format_result_response(result_dict, status=COMPLETED):
    """Формирует строку ответа с результатами для отправки клиенту."""
    parts = [f"RESULT:{status}"]
    if result_dict:
        parts.append(f"RA:{result_dict.get('ra', '')}")
        parts.append(f"DEC:{result_dict.get('dec', '')}")
        parts.append(f"RA_HMS:{result_dict.get('ra_hms', '')}")
        parts.append(f"DEC_DMS:{result_dict.get('dec_dms', '')}")
        parts.append(f"PIXEL_SCALE:{result_dict.get('pixel_scale', '')}")
        parts.append(f"FIELD_SIZE:{result_dict.get('field_size', '')}")
        parts.append(f"ROTATION:{result_dict.get('rotation_angle', '')}")
        parts.append(f"PARITY:{result_dict.get('parity', '')}")
    return "|".join(parts)


def handle_client(conn, addr):
    """Обработчик подключений клиента."""
    global solve_state
    print(f"[CONNECT] Новое соединение: {addr}.")
    
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break  # Клиент отключился
            
            message = data.decode('utf-8').strip()
            print(f"[REQUEST] {addr}: {message}")
            
            response = ""
            
            # === Базовый протокол ===
            if message == "PING":
                response = "PONG"
            
            # === Запуск Plate-Solving ===
            elif message.startswith("SOLVE:"):
                parts = message.split('|')
                image_path = parts[0][6:].strip()
                timeout_sec = None
                if len(parts) > 1:
                    timeout_sec = int(parts[1][6:].strip())
                
                with state_lock:
                    if solve_state['status'] in (RUNNING, INITIALIZING):
                        response = "ERROR:solve_already_running"
                    else:
                        # Сброс состояния
                        solve_state['status'] = INITIALIZING
                        solve_state['result'] = None
                        solve_state['error'] = None
                        solve_state['output'] = None
                        
                        # Запуск в отдельном потоке
                        thread = threading.Thread(target=run_solve_field, args=(image_path,timeout_sec,))
                        thread.daemon = True
                        thread.start()
                        solve_state['thread'] = thread
                        response = "OK:solve_started"
            
            # === Отмена Plate-Solving ===
            elif message == "CANCEL":
                if cancel_solve():
                    response = f"OK:{CANCELLED}"
                else:
                    response = "OK:no_active_solve"
            
            # === Запрос статуса ===
            elif message == "STATUS":
                with state_lock:
                    status = solve_state['status']
                    if status == COMPLETED:
                        response = f"STATUS:{COMPLETED}"
                    elif status == FAILED:
                        response = f"STATUS:{FAILED}"
                    elif status == CANCELLED:
                        response = f"STATUS:{CANCELLED}"
                    elif status == RUNNING:
                        response = f"STATUS:{RUNNING}"
                    elif status == INITIALIZING:
                        response = f"STATUS:{INITIALIZING}"
                    else:
                        response = f"STATUS:{IDLE}"
            
            # === Запрос результата ===
            elif message == "RESULT":
                with state_lock:
                    status = solve_state['status']
                    if status == COMPLETED and solve_state['result']:
                        response = format_result_response(solve_state['result'], COMPLETED)
                    elif solve_state['status'] == FAILED:
                        error = solve_state.get('error', 'Unknown error')
                        response = f"RESULT:{FAILED}|ERROR:{error}"
                    else:
                        response = "RESULT:NO_DATA"
                    # Сброс состояния при COMPLETED и FAILED
                    if status == COMPLETED and solve_state['result'] or solve_state['status'] == FAILED:
                        solve_state['process'] = None
                        solve_state['thread'] = None
                        solve_state['status'] = IDLE
                        solve_state['output'] = None
                        solve_state['result'] = None
                        solve_state['error'] = None
                        solve_state['start_time'] = None
            
            # === Неизвестная команда ===
            else:
                response = "UNKNOWN"
            
            # Отправка ответа
            if response:
                conn.send(response.encode('utf-8'))
                
    except ConnectionResetError:
        print(f"[DISCONNECT] Клиент {addr} разорвал соединение.")
    except Exception as e:
        print(f"[ERROR] {type(e).__name__}: {e}")
    finally:
        conn.close()
        print(f"[DISCONNECT] Соединение с {addr} закрыто.")
        cancel_solve()


def start():
    """Запуск сервера."""
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        server.bind((HOST, PORT))
        server.listen(5)  # Очередь подключений
        print(f"[SERVER] Запущен на {HOST}:{PORT}")
        print(f"[INFO] Ожидаю подключения клиентов...")
        
        while True:
            conn, addr = server.accept()
            # Каждый клиент в отдельном потоке
            thread = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
            thread.start()
            
    except KeyboardInterrupt:
        print("\n[SERVER] Получен сигнал остановки...")
        cancel_solve()
    except Exception as e:
        print(f"[SERVER ERROR] {e}")
    finally:
        server.close()
        print("[SERVER] Сервер остановлен.")


if __name__ == "__main__":
    start()