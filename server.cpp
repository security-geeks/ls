#include "server.h"
#include "ui_server.h"
#include <QGraphicsView>
#include <qrencode/qrencode.h>

extern QString ip;                 //ip
extern QString broadcast;                  //广播地址

server::server(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::server)
{
    setAttribute(Qt::WA_DeleteOnClose);             //关闭窗口后调用析构函数
    ui->setupUi(this);

    //添加工具栏
    QToolBar *toolbar = new QToolBar(this); //变量还必须在堆中
    toolbar->setIconSize(QSize(16,16));
    toolbar->addAction(QString("主页"));
    toolbar->addAction(QString("传输文件"));
    toolbar->addAction(QString("传输中"));
    toolbar->addAction(QString("传输历史"));
    toolbar->addAction(QString("二维码"));
    connect(toolbar,SIGNAL(actionTriggered(QAction*)),this,SLOT(toolbar_actiontriggered(QAction*)));    //处理工具栏点击
    ui->tool->layout()->addWidget(toolbar);
    ui->tool->layout()->setAlignment(Qt::AlignTop);

    ui->transfering->hide();
    ui->qrcode->hide();
    ui->verticalLayout_3->setAlignment(Qt::AlignTop);

    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);   //表格禁止编辑
    connect(ui->tableWidget,SIGNAL(cellClicked(int,int)),this,SLOT(tablewidget_clicked(int,int)));

    lastpoint.setX(0);
    lastpoint.setY(0);
    endpoint.setX(0);
    endpoint.setY(0);

    udpbro = NULL;  //udp广播
    socketinit();   //初始化udp广播、tcp监听
    //初始化http_server
    http_server = new httpserver(this);
    http_server->listen(QHostAddress::Any,80);

}

server::~server()
{
    delete udpsender;
    delete tcpServer;
    udpbro->terminate();
    udpbro->wait();
    delete udpbro;
}

void server::updatetabelwidget(QByteArray mess, tcpsocket * clientsocket,QString id)
{
    qDebug()<<"有新数据updatetabelwidget";
    QJsonParseError error;
    QJsonDocument jsondoc = QJsonDocument::fromJson(mess,&error);       //转化成json对象
    QVariantMap result = jsondoc.toVariant().toMap();
    if(result["type"].toString() == QString("location")){
        QString ip = clientsocket->peerAddress().toString();
        QString port = QString("%1").arg(clientsocket->peerPort());

        int row = ui->tableWidget->rowCount();

        for(int i = 0;i<row;i++){
            if(ui->tableWidget->item(i,0)->text() == id ){
                ui->tableWidget->removeCellWidget(i,1);             //先清掉
                ui->tableWidget->removeCellWidget(i,2);             //先清掉
                ui->tableWidget->removeCellWidget(i,3);             //先清掉
                ui->tableWidget->removeCellWidget(i,4);             //先清掉
                ui->tableWidget->setItem(i,1,new QTableWidgetItem(ip));     //更新ip地址
                ui->tableWidget->setItem(i,2,new QTableWidgetItem(port));         //更新端口
                ui->tableWidget->setItem(i,3,new QTableWidgetItem(result["x"].toString()));       //更新横坐标
                ui->tableWidget->setItem(i,4,new QTableWidgetItem(result["y"].toString()));           //更新纵坐标
                break;
            }
        }
        //画图并更改卫星实时位置
        QHashIterator <QString,tcpsocket *> i(this->tcpServer->tcpClientSocketList);
        while(i.hasNext()){
            i.next();
            if(i.key() == id){
                inf pretem;         //前一个位置结构
                pretem = locationlist.value(i.key());
                inf tem;            //现在的位置结构
                tem = pretem;
                tem.x = result["x"].toFloat();
                tem.y = result["y"].toFloat();
                tem.sview_widget->x = tem.x;
                tem.sview_widget->y = tem.y;
                tem.sview_widget->inf_update();
                if(pretem.x == -1)//发送的数据点不足两个的情况
                {
                    tem.lineItemNum=0;

                    //设置画笔的属性、刷子的属性，用于绘制轨迹和头部矩形
                    pen.setColor(tem.color);
                    pen.setWidth(2);
                    brush.setStyle(Qt::SolidPattern);
                    brush.setColor(tem.color);

                    //设置头部矩形,并添加到场景中
                    //scene.addRect(tem.x-10,tem.y-10,20,20,pen,brush);
                    tem.headRect = new headRectItem(tem.x-10,tem.y-10,20,20,i);
                    tem.headRect->setPen(pen);
                    tem.headRect->setBrush(brush);
                    scene.addItem(tem.headRect);

                    //inf结构体的数据都设置好了，插入locationlist中
                    locationlist.insert(i.key(),tem);
                }

                //设置起始点
                qDebug() << pretem.x << "  " << pretem.y << "  "
                         << tem.x << "  " << tem.y << endl;

                if(pretem.x != -1)
                {
                    //设置所画轨迹线段的起点和终点
                    lastpoint.setX(pretem.x);
                    lastpoint.setY(pretem.y);
                    endpoint.setX(tem.x);
                    endpoint.setY(tem.y);

                    //设置画笔和画刷的属性
                    pen.setColor(pretem.color);//设置颜色
                    pen.setWidth(2);
                    brush.setStyle(Qt::SolidPattern);
                    brush.setColor(pretem.color);

                    //读取轨迹数量
                    lineItemNum=pretem.lineItemNum;
                    //读取轨迹段数据指针
                    lineItemPointer=tem.lineItemPointer;

                    //从这里开始正式画图
                    //在场景scene中添加新一段轨迹LineItem，同时将lineitem的数量+1，并用指针lineItemPointer进行记录
                    lineItemPointer[lineItemNum++] = scene.addLine(lastpoint.x(),lastpoint.y(),endpoint.x(),endpoint.y());

                    //设置画轨迹的画笔颜色
                    lineItemPointer[lineItemNum-1]->setPen(pen);

                    //删除上一段轨迹的头部矩形
                    scene.removeItem(pretem.headRect);

                    //添新建当前轨迹的头部矩形，并添加到场景scene中
                    //tem.headRect = scene.addRect(endpoint.x()-10,endpoint.y()-10,20,20,pen,brush);
                    tem.headRect = new headRectItem(tem.x-10,tem.y-10,20,20,i);
                    tem.headRect->setPen(pen);
                    tem.headRect->setBrush(brush);
                    scene.addItem(tem.headRect);

                    //为了防止轨迹太多，画面太乱，轨迹段数量超过一定量后就删减掉旧数据
                    if(lineItemNum>10)
                    {
                        scene.removeItem(lineItemPointer[0]);
                        for(int j=0;j<lineItemNum-1;j++)
                        {
                            lineItemPointer[j] = lineItemPointer[j+1];
                        }
                        lineItemNum--;
                    }

                    //记录轨迹段数量
                    tem.lineItemNum=lineItemNum;

                    //将最新的inf结构体数据加入到locationlist中
                    locationlist.insert(i.key(),tem);
                    qDebug() << "卫星id" << i.key() << endl;
                    //将场景scene显示在server.ui的graphicsView中，即是画出图像
                    ui->graphicsView->setRenderHint(QPainter::Antialiasing);//抗锯齿
                    ui->graphicsView->setScene(&scene);
                }

                break;
            }
        }

    }else{
        qDebug()<<"收到的信息不是位置信息";
        qDebug()<<mess;
    }
}

void server::updatenewclient(QString id,tcpsocket * clientsocket)
{
    int row = ui->tableWidget->rowCount();
    ui->tableWidget->setRowCount(row+1);
    ui->tableWidget->setItem(row,0,new QTableWidgetItem(id));                   //更新卫星id
    ui->tableWidget->setItem(row,1,new QTableWidgetItem(clientsocket->peerAddress().toString()));       //更新新连接ip
    ui->tableWidget->setItem(row,2,new QTableWidgetItem(QString("%1").arg(clientsocket->peerPort())));      //更新新连接端口

    QTableWidgetItem *tem = new QTableWidgetItem("断开");
    tem->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->setItem(row,5,tem);

    QTableWidgetItem *tem1 = new QTableWidgetItem("发送指令");
    tem1->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->setItem(row,6,tem1);

    //添加卫星实时位置
    if(this->tcpServer->tcpClientSocketList.contains(id)){
        inf tem;
        tem.x = -1;
        tem.y = -1;
        tem.color = QColor(qrand() % 256, qrand() %256, qrand() % 256);//设置颜色，用于绘制轨迹
        tem.sview_widget = new sinfview(id,clientsocket->peerAddress().toString()+QString(":%1").arg(clientsocket->peerPort()),tem.x,tem.y);

        //tem.color = Qt::green;
        qDebug() << "tem.color was set" << endl;
        locationlist.insert(id,tem);
        qDebug()<<"卫星id列表"<<locationlist.keys();
    }
}

void server::disconnected(tcpsocket *clientsocket,QString id)
{
    int row = ui->tableWidget->rowCount();
    QString ip = clientsocket->peerAddress().toString();
    QString port = QString("%1").arg(clientsocket->peerPort());
    for(int i = 0;i<row;i++){
        if(ui->tableWidget->item(i,1)->text() == ip
           && ui->tableWidget->item(i,2)->text() == port){
            ui->tableWidget->removeRow(i);
            break;
        }
    }
    //删除列表中对应项
    inf tem = locationlist.value(id);
    tem.sview_widget->deleteLater();
    locationlist.remove(id);
//    free(clientsocket);                    //释放这个不用的连接的内存  此处要用free 不能用delete 会出错(如何释放这个连接有待商榷)
}


void server::wificonnected()
{
    //清除
    delete udpsender;
    delete tcpServer;
    udpbro->stop();
    socketinit();
}

void server::socketinit()
{
    udpsender = new QUdpSocket(this);                   //实例化udpsender 对象
    tcpServer = new tcpserver(this);                   //实例化tcpserver对象
    udpsender->bind(QHostAddress(ip),0);
    ui->multicastip->setText(broadcast);         //设置udp广播地址

    //加入广播组
    udpsender->joinMulticastGroup(QHostAddress(ui->multicastip->text()));

    //获取监听的端口和ip  udp广播发送的信息
    QJsonObject json;
    json.insert("ip",ip);
    json.insert("port",ui->tcpport->text().toInt());
    QJsonDocument document;
    document.setObject(json);
    QByteArray datagram = document.toJson(QJsonDocument::Compact);

    //启动udp广播线程
    if(NULL == udpbro)
        udpbro = new udpbroad(udpsender,datagram,ui->udpport->text().toInt(),ui->multicastip->text());
    else
        udpbro->reset(udpsender,datagram,ui->udpport->text().toInt(),ui->multicastip->text());
    udpbro->start();

    //监听tcp
    if(!this->tcpServer->isListening()){
        if(!this->tcpServer->listen(QHostAddress(ip),ui->tcpport->text().toInt()))
        {
            qDebug() << this->tcpServer->errorString();
        }else{

        }
        ui->pushButton_2->setText(QString("正在监听"));
    }else{
        this->tcpServer->close();   //如果正在监听则关闭
        ui->pushButton_2->setText(QString("开始监听"));
    }

    //关联新的tcp连接产生与更新界面
    connect(tcpServer,SIGNAL(newclientsocket(QString,tcpsocket*)),this,SLOT(updatenewclient(QString,tcpsocket*)));
    //关联接收数据的信号与更新界面
    connect(tcpServer,SIGNAL(updateServer(QByteArray,tcpsocket*,QString)),this,SLOT(updatetabelwidget(QByteArray,tcpsocket*,QString)));
    //关联连接断开与更新界面
    connect(tcpServer,SIGNAL(disconnected(tcpsocket*,QString)),this,SLOT(disconnected(tcpsocket*,QString)));
    //关联文件接收信号
    connect(tcpServer,SIGNAL(updateServer_file(qint64,qint64,QString,tcpsocket*,QString)),this,SLOT(updatefileview(qint64,qint64,QString,tcpsocket*,QString)));
    //关联新文件接收信号
    connect(tcpServer,SIGNAL(updateServer_newfile(qint64,qint64,QString,tcpsocket*,QString)),this,SLOT(updatefileview_new(qint64,qint64,QString,tcpsocket*,QString)));
}

void server::tablewidget_clicked(int row, int colum)
{
    if(colum == 5){
        QString id = ui->tableWidget->item(row,0)->text();
        //断开连接
        tcpServer->tcpClientSocketList.value(id)->close();
        //从列表中删除
        tcpServer->tcpClientSocketList.remove(id);
        qDebug()<<row<<colum;
    }
    if(colum == 6){
        Instruction tem;
        tem.setWindowTitle("给"+ui->tableWidget->item(row,0)->text()+"发送指令");
        if(tem.exec() == QDialog::Accepted){
            //发送指令信息
            QJsonParseError error;
            QJsonDocument jsondoc = QJsonDocument::fromJson(tem.order.toLatin1(),&error);       //转化成json对象
            qDebug()<<(error.error==0);
            qDebug()<<tem.order;
            QString id = ui->tableWidget->item(row,0)->text();
            tcpsocket * tem1 = tcpServer->tcpClientSocketList.value(id);
            if(tem1->isWritable())
                tem1->write(tem.order.toLatin1());
        }else{


        }
    }

}

void server::toolbar_actiontriggered(QAction *tem)
{
    //工具栏按钮点击
    if(tem->text() == QString("主页")){
        ui->homepage->show();
    }else{
        ui->homepage->hide();
    }

    if(tem->text() == QString("传输中")){
        ui->transfering->show();
    }else{
        ui->transfering->hide();
    }

    if(tem->text() == QString("传输文件")){

    }
    if(tem->text() == QString("二维码")){
        QImage ret;
        int bulk = 8;
        QString str = QString("http://")+ip;
        QRcode* qr = QRcode_encodeString(str.toUtf8(), 1, QR_ECLEVEL_Q, QR_MODE_8, 0);
        if ( qr != nullptr )
        {
            int allBulk = (qr->width) * bulk;
            ret = QImage(allBulk, allBulk, QImage::Format_Mono);
            QPainter painter(&ret);
            QColor fg("black");
            QColor bg("white");
            painter.setBrush(bg);
            painter.setPen(Qt::NoPen);
            painter.drawRect(0, 0, allBulk, allBulk);

            painter.setBrush(fg);
            for( int y=0; y<qr->width; y++ )
            {
                for( int x=0; x<qr->width; x++ )
                {
                    if ( qr->data[y*qr->width+x] & 1 )
                    {
                        QRectF r(x*bulk, y*bulk, bulk, bulk);
                        painter.drawRects(&r, 1);
                    }
                }
            }
            QRcode_free(qr);
        }
        ui->qrcode_l->setPixmap(QPixmap::fromImage(ret));
        ui->qrcode->show();
    }else{
        ui->qrcode->hide();
    }
}

void server::updatefileview(qint64 bytesreveived, qint64 totalbytes, QString filename, tcpsocket *clientsocket, QString id)
{
    QString name = id+QString("/")+filename;
    if(this->filelist.contains(name)){
        QProgressBar *tem = this->filelist.value(name);
        tem->setValue(bytesreveived);
    }
    if(totalbytes == bytesreveived){
        //从filelist中删除
        this->filelist.remove(name);
    }
}

void server::updatefileview_new(qint64 bytesreveived, qint64 totalbytes, QString filename, tcpsocket *clientsocket, QString id)
{
    qDebug()<<"更新界面";
    QString name = id+QString("/")+filename;
    QHBoxLayout *tem = new QHBoxLayout();
    tem->setSpacing(6);
    QLabel *tem1 = new QLabel(name,ui->transfering);
    QProgressBar *tem2 = new QProgressBar(ui->transfering);
    tem2->setValue(bytesreveived);
    tem2->setMaximum(totalbytes);
    if(!this->filelist.contains(name)){
        this->filelist.insert(name,tem2);
    }else{
        this->filelist.value(name,tem2);
    }
    tem2->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    tem->addWidget(tem1);
    tem->addWidget(tem2);
    QVBoxLayout *translayout =(QVBoxLayout *) ui->transfering->layout();
    translayout->addLayout(tem);
}


